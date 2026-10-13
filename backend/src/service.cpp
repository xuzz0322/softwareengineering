#include "service.hpp"
#include "domain.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
using Clock = std::chrono::steady_clock;
namespace {
void require(bool ok, int status, const std::string& message) { if (!ok) throw ApiError(status, message); }
std::string string_field(const json& data, const char* field, size_t maximum = 120) {
    require(data.contains(field) && data[field].is_string(), 400, std::string("缺少字段：") + field);
    auto value = data[field].template get<std::string>();
    require(!value.empty() && value.size() <= maximum && value.find('\0') == std::string::npos, 400, std::string("字段长度无效：") + field);
    return value;
}
json body(const httplib::Request& req) {
    require(req.get_header_value("Content-Type").find("application/json") == 0, 415, "请使用 JSON 请求");
    auto data = json::parse(req.body); require(data.is_object(), 400, "请求必须为 JSON 对象"); return data;
}
std::string id_string(const json& value) { require(value.is_number_integer() && value.template get<int>() > 0, 400, "编号无效"); return std::to_string(value.template get<int>()); }
json current_term(Db& db) { return db.query("SELECT *, CURRENT_DATE BETWEEN add_drop_start AND add_drop_end AS in_window FROM semesters ORDER BY id DESC LIMIT 1").at(0); }
void registration_open(const json& term) {
    require(term["status"] == "OPEN", 409, "选课已关闭，无法修改");
    require(term["in_window"] == 1, 409, "当前不在选退课时间范围内");
}
json user_public(const json& row) { return {{"id", row["id"]}, {"username", row["username"]}, {"name", row["name"]}, {"role", row["role"]}, {"department", row["department"]}}; }
std::string token_of(const httplib::Request& req) {
    auto cookie = req.get_header_value("Cookie");
    size_t pos = 0;
    while (pos < cookie.size()) {
        size_t end = cookie.find(';', pos); auto part = cookie.substr(pos, end == std::string::npos ? end : end - pos);
        size_t first = part.find_first_not_of(' '); if (first != std::string::npos) part.erase(0, first);
        if (part.rfind("cr_session=", 0) == 0) return part.substr(11);
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return "";
}
json authenticate(Db& db, const httplib::Request& req) {
    auto token = token_of(req); require(token.size() == 64, 401, "请先登录");
    auto rows = db.query("SELECT u.*, s.csrf_token FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token_hash=? AND s.expires_at>UTC_TIMESTAMP() AND u.active=1", {sha256(token)});
    require(!rows.empty(), 401, "登录已过期，请重新登录");
    if (req.method != "GET") require(constant_equal(rows[0]["csrf_token"], req.get_header_value("X-CSRF-Token")), 403, "安全校验失败，请刷新页面");
    return rows[0];
}
void role(const json& user, const char* wanted) { require(user["role"] == wanted, 403, "没有执行此操作的权限"); }
std::string uid(const json& user) { return std::to_string(user["id"].template get<int>()); }
// The session is authoritative; explicitly reject attempted foreign ownership.
void require_own_schedule(const httplib::Request& req, const json& user) {
    for (const auto& param : req.params)
        if (param.first == "student_id")
            require(param.second == uid(user), 403, "只能操作本人的课程表");
    if (!req.body.empty()) {
        const auto data = body(req);
        if (data.contains("student_id"))
            require(id_string(data["student_id"]) == uid(user), 403, "只能操作本人的课程表");
    }
}
void audit(Db& db, const json& user, const std::string& action, const json& detail) {
    db.query("INSERT INTO audit_logs(actor_id,action,detail) VALUES(?,?,?)", {uid(user), action, detail.dump()});
}
std::mutex catalog_mutex;
json catalog_cache;
Clock::time_point catalog_at;
json catalog(Db& db) {
    std::lock_guard<std::mutex> lock(catalog_mutex);
    if (catalog_cache.is_null() || Clock::now() - catalog_at > std::chrono::seconds(60)) {
        // SELECT is the only permission granted for legacy_catalog, even if code is changed accidentally.
        catalog_cache = db.query("SELECT * FROM legacy_catalog.courses ORDER BY id");
        for (auto& c : catalog_cache) c["prerequisites"] = json::parse(c["prerequisites"].template get<std::string>());
        catalog_at = Clock::now();
    }
    return catalog_cache;
}
json offerings(Db& db, int semester) {
    auto courses = catalog(db);
    auto rows = db.query("SELECT o.*,u.name AS professor_name,(SELECT COUNT(*) FROM enrollments e WHERE e.offering_id=o.id) AS enrolled FROM offerings o LEFT JOIN users u ON u.id=o.professor_id WHERE semester_id=? ORDER BY o.id", {std::to_string(semester)});
    for (auto& o : rows) for (const auto& c : courses) if (o["course_id"] == c["id"]) {
        for (const char* k : {"code", "title", "department", "credits", "fee_cents", "prerequisites"}) o[k] = c[k];
        o["prerequisite_names"] = json::array();
        for (const auto& p : c["prerequisites"]) for (const auto& target : courses) if (target["id"] == p) o["prerequisite_names"].push_back(target["title"]);
    }
    return rows;
}
json* find_offering(json& rows, int id) { for (auto& o : rows) if (o["id"] == id) return &o; return nullptr; }
std::set<int> passed_courses(Db& db, int student) {
    std::set<int> result;
    for (const auto& r : db.query("SELECT DISTINCT o.course_id FROM enrollments e JOIN offerings o ON o.id=e.offering_id JOIN semesters s ON s.id=o.semester_id WHERE e.student_id=? AND e.grade IN ('A','B','C','D') AND s.status='COMPLETED'", {std::to_string(student)})) result.insert(r["course_id"].template get<int>());
    return result;
}
void validate_choices(json& all, const json& primary, const json& alternates, bool submit) {
    require(primary.is_array() && alternates.is_array(), 400, "课程选择必须为数组");
    require(primary.size() <= 4 && alternates.size() <= 2, 400, "最多选择 4 门主选和 2 门备选");
    if (submit) require(primary.size() == 4 && alternates.size() == 2, 400, "提交时必须选择 4 门主选和 2 门备选");
    std::set<int> courses;
    for (const auto& list : {primary, alternates}) for (const auto& id : list) {
        id_string(id); auto* o = find_offering(all, id.template get<int>());
        require(o && (*o)["status"] == "open", 400, "选择包含未开放或不存在的教学班");
        require(courses.insert((*o)["course_id"].template get<int>()).second, 400, "主选与备选不能重复选择同一门课程");
    }
}
json get_schedule(Db& db, const json& user, int semester) {
    auto rows = db.query("SELECT * FROM schedules WHERE student_id=? AND semester_id=?", {uid(user), std::to_string(semester)});
    if (rows.empty()) return {{"primary", json::array()}, {"alternates", json::array()}, {"status", "EMPTY"}, {"enrolled", json::array()}};
    json s = rows[0]; s["primary"] = json::parse(s["primary_choices"].template get<std::string>());
    s["alternates"] = json::parse(s["alternate_choices"].template get<std::string>());
    s.erase("primary_choices"); s.erase("alternate_choices"); s.erase("submitted_alternates");
    s["enrolled"] = json::array();
    for (const auto& r : db.query("SELECT e.offering_id FROM enrollments e JOIN offerings o ON o.id=e.offering_id WHERE e.student_id=? AND o.semester_id=? ORDER BY o.id", {uid(user), std::to_string(semester)})) s["enrolled"].push_back(r["offering_id"]);
    return s;
}
using Handler = std::function<json(Db&, const httplib::Request&, const json&)>;
httplib::Server::Handler api(Handler handler, bool login = true) {
    return [handler, login](const httplib::Request& req, httplib::Response& res) {
        try { Db db; json user = login ? authenticate(db, req) : json::object(); res.set_content(handler(db, req, user).dump(), "application/json; charset=utf-8"); }
        catch (const ApiError& e) { res.status = e.status; res.set_content(json{{"error", e.what()}}.dump(), "application/json; charset=utf-8"); }
        catch (const json::exception&) { res.status = 400; res.set_content(json{{"error", "请求字段或格式不正确"}}.dump(), "application/json; charset=utf-8"); }
        catch (const std::exception& e) { std::cerr << req.method << " " << req.path << ": " << e.what() << std::endl; res.status = 503; res.set_content(json{{"error", "服务暂时不可用，请稍后重试"}}.dump(), "application/json; charset=utf-8"); }
    };
}
bool valid_date(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (size_t i = 0; i < s.size(); ++i) if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
    int y = std::stoi(s.substr(0,4)), m = std::stoi(s.substr(5,2)), d = std::stoi(s.substr(8,2));
    int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 1900 || y > 2100 || m < 1 || m > 12) return false;
    if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) days[2] = 29;
    return d >= 1 && d <= days[m];
}
std::mutex attempts_mutex;
std::map<std::string, std::pair<int, Clock::time_point>> attempts;
void throttle(const std::string& key) {
    std::lock_guard<std::mutex> lock(attempts_mutex);
    auto now = Clock::now();
    for (auto it = attempts.begin(); it != attempts.end();) if (now - it->second.second > std::chrono::minutes(5)) it = attempts.erase(it); else ++it;
    auto& v = attempts[key]; if (v.first == 0) v.second = now;
    require(++v.first <= 60, 429, "登录尝试过于频繁，请五分钟后重试");
}
std::atomic<bool> demo_billing_available{true};
}
void bootstrap_passwords() {
    Db db;
    require(db.query("SELECT COUNT(*) AS n FROM users")[0]["n"].get<int>() > 0, 500, "数据库种子数据未完成导入");
    auto rows = db.query("SELECT id FROM users WHERE password_hash IS NULL");
    if (rows.empty()) return;
    auto password = env("DEMO_PASSWORD");
    require(password.size() >= 10, 500, "首次启动请配置至少 10 位的 DEMO_PASSWORD");
    Transaction tx(db);
    for (const auto& row : rows) { auto salt = random_hex(16); db.query("UPDATE users SET password_salt=?,password_hash=? WHERE id=? AND password_hash IS NULL", {salt, password_hash(password, salt), uid(row)}); }
    tx.commit();
}
void configure_routes(httplib::Server& server) {
    server.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("X-Content-Type-Options", "nosniff"); res.set_header("X-Frame-Options", "DENY");
        res.set_header("Referrer-Policy", "same-origin");
        res.set_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
        if (req.path.rfind("/api/",0) == 0) res.set_header("Cache-Control", "no-store");
    });
    server.Get("/api/health", api([](Db& db, const auto&, const auto&) { db.query("SELECT 1"); return json{{"status","ok"}}; }, false));
    server.Post("/api/login", [&server](const httplib::Request& req, httplib::Response& res) {
        (void)server;
        api([&res](Db& db, const auto& r, const auto&) {
            throttle(r.remote_addr); auto data = body(r);
            auto username = string_field(data,"username",64), password = string_field(data,"password",128);
            auto rows = db.query("SELECT * FROM users WHERE username=? AND active=1", {username});
            auto calculated = password_hash(password, rows.empty() ? std::string(32,'0') : rows[0]["password_salt"].template get<std::string>());
            require(!rows.empty() && constant_equal(calculated, rows[0]["password_hash"]), 401, "用户名或密码错误");
            auto token = random_hex(32), csrf = random_hex(32);
            db.query("DELETE FROM sessions WHERE expires_at<=UTC_TIMESTAMP()");
            db.query("INSERT INTO sessions(token_hash,user_id,csrf_token,expires_at) VALUES(?,?,?,DATE_ADD(UTC_TIMESTAMP(),INTERVAL 2 HOUR))", {sha256(token),uid(rows[0]),csrf});
            res.set_header("Set-Cookie", "cr_session=" + token + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=7200" + (env("COOKIE_SECURE") == "1" ? "; Secure" : ""));
            audit(db,rows[0],"LOGIN",json::object()); return json{{"user",user_public(rows[0])},{"csrf",csrf}};
        }, false)(req,res);
    });
    server.Post("/api/logout", [](const httplib::Request& req, httplib::Response& res) {
        api([&res](Db& db, const auto& r, const auto&) {
            db.query("DELETE FROM sessions WHERE token_hash=?", {sha256(token_of(r))});
            res.set_header("Set-Cookie", "cr_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0"); return json{{"ok",true}};
        })(req,res);
    });
    server.Get("/api/me", api([](Db& db, const auto&, const json& u) { return json{{"user",user_public(u)},{"csrf",u["csrf_token"]},{"semester",current_term(db)}}; }));
    server.Get("/api/offerings", api([](Db& db, const auto&, const json& u) {
        auto t = current_term(db); json out{{"semester",t},{"offerings",offerings(db,t["id"] )},{"courses",catalog(db)}};
        if (u["role"] == "STUDENT") out["passed_courses"] = passed_courses(db,u["id"]);
        if (u["role"] == "PROFESSOR") { out["qualifications"] = json::array(); for (const auto& q : db.query("SELECT course_id FROM teaching_qualifications WHERE professor_id=?",{uid(u)})) out["qualifications"].push_back(q["course_id"]); }
        return out;
    }));
    server.Get("/api/schedule", api([](Db& db, const auto& req, const json& u) { role(u,"STUDENT"); require_own_schedule(req,u); return get_schedule(db,u,current_term(db)["id"]); }));
    server.Put("/api/schedule", api([](Db& db, const auto& req, const json& u) {
        role(u,"STUDENT"); require_own_schedule(req,u); auto data = body(req); bool submit = data.value("submit",false);
        auto primary = data.at("primary"), alternates = data.at("alternates");
        Transaction tx(db); auto term = current_term(db); registration_open(term);
        auto all = offerings(db,term["id"]); validate_choices(all,primary,alternates,submit);
        auto semester = std::to_string(term["id"].template get<int>());
        if (submit) {
            // Remove old seats inside the same transaction. Any validation failure restores them.
            db.query("DELETE e FROM enrollments e JOIN offerings o ON o.id=e.offering_id WHERE e.student_id=? AND o.semester_id=?",{uid(u),semester});
            all = offerings(db,term["id"]); auto passed = passed_courses(db,u["id"]); json chosen = json::array();
            for (const auto& id : primary) {
                auto* o = find_offering(all,id.template get<int>());
                auto error = domain::can_add(*o,chosen,passed);
                require(error.empty(),409,(*o)["title"].template get<std::string>() + "：" + error);
                chosen.push_back(*o);
            }
            for (const auto& id : primary) db.query("INSERT INTO enrollments(student_id,offering_id) VALUES(?,?)",{uid(u),id_string(id)});
        }
        db.query("INSERT INTO schedules(student_id,semester_id,primary_choices,alternate_choices,submitted_alternates,status) VALUES(?,?,?,?,?,?) ON DUPLICATE KEY UPDATE primary_choices=VALUES(primary_choices),alternate_choices=VALUES(alternate_choices),status=VALUES(status),submitted_alternates=IF(VALUES(status)='SUBMITTED',VALUES(submitted_alternates),submitted_alternates)",
            {uid(u),semester,primary.dump(),alternates.dump(),submit ? alternates.dump() : "[]",submit ? "SUBMITTED" : "DRAFT"});
        audit(db,u,submit ? "SUBMIT_SCHEDULE" : "SAVE_DRAFT",{{"primary",primary},{"alternates",alternates}});
        tx.commit(); return get_schedule(db,u,term["id"]);
    }));
    server.Delete("/api/schedule", api([](Db& db, const auto& req, const json& u) {
        role(u,"STUDENT"); require_own_schedule(req,u); Transaction tx(db); auto t=current_term(db); registration_open(t); auto s=std::to_string(t["id"].template get<int>());
        require(!db.query("SELECT student_id FROM schedules WHERE student_id=? AND semester_id=?",{uid(u),s}).empty(),404,"尚未创建课表");
        db.query("DELETE e FROM enrollments e JOIN offerings o ON o.id=e.offering_id WHERE e.student_id=? AND o.semester_id=?",{uid(u),s});
        db.query("DELETE FROM schedules WHERE student_id=? AND semester_id=?",{uid(u),s}); audit(db,u,"DELETE_SCHEDULE",{{"semester_id",t["id"]}}); tx.commit(); return json{{"ok",true}};
    }));
    server.Get("/api/report-card", api([](Db& db, const auto&, const json& u) {
        role(u,"STUDENT");
        auto rows=db.query("SELECT e.grade,o.course_id,o.section,s.name AS semester,s.id AS semester_id,u.name AS professor_name FROM enrollments e JOIN offerings o ON o.id=e.offering_id JOIN semesters s ON s.id=o.semester_id LEFT JOIN users u ON u.id=o.professor_id WHERE e.student_id=? AND s.status='COMPLETED' ORDER BY s.id DESC,o.id",{uid(u)});
        for (auto& r:rows) for (const auto& c:catalog(db)) if (r["course_id"]==c["id"]) {r["title"]=c["title"];r["code"]=c["code"];r["credits"]=c["credits"];}
        return rows;
    }));
    server.Put("/api/teaching", api([](Db& db,const auto& req,const json& u) {
        role(u,"PROFESSOR"); auto ids=body(req).at("offerings"); require(ids.is_array()&&ids.size()<=30,400,"授课列表无效");
        Transaction tx(db);auto t=current_term(db);registration_open(t);auto all=offerings(db,t["id"]);json chosen=json::array();std::set<int> seen,qual;
        for(const auto& q:db.query("SELECT course_id FROM teaching_qualifications WHERE professor_id=?",{uid(u)}))qual.insert(q["course_id"].template get<int>());
        for(const auto& id:ids){id_string(id);auto* o=find_offering(all,id.template get<int>());require(o && (*o)["status"]=="open",400,"教学班不存在或未开放");require(seen.insert(id.template get<int>()).second,400,"教学班不能重复");require((*o)["professor_id"].is_null()||(*o)["professor_id"]==u["id"],409,"教学班已有其他教师");require(qual.count((*o)["course_id"].template get<int>()),403,"不具备该课程的授课资格");for(const auto& c:chosen)require(!domain::conflicts(c,*o),409,"授课时间冲突");chosen.push_back(*o);}
        db.query("UPDATE offerings SET professor_id=NULL WHERE professor_id=? AND semester_id=?",{uid(u),std::to_string(t["id"].template get<int>())});
        for(const auto& id:ids)db.query("UPDATE offerings SET professor_id=? WHERE id=?",{uid(u),id_string(id)});
        audit(db,u,"SELECT_TEACHING",{{"offerings",ids}});tx.commit();return json{{"ok",true}};
    }));
    server.Get("/api/teaching", api([](Db& db,const auto&,const json& u){role(u,"PROFESSOR");json result=json::array();for(const auto& t:db.query("SELECT * FROM semesters ORDER BY id DESC"))for(auto o:offerings(db,t["id"]))if(o["professor_id"]==u["id"]){o["semester_name"]=t["name"];o["semester_status"]=t["status"];result.push_back(o);}return result;}));
    server.Get(R"(/api/offerings/(\d+)/roster)",api([](Db& db,const auto& req,const json& u){
        role(u,"PROFESSOR");auto id=req.matches[1].str();auto rows=db.query("SELECT id FROM offerings WHERE id=? AND professor_id=?",{id,uid(u)});require(!rows.empty(),403,"只能查看本人授课班级的名单");
        return db.query("SELECT u.id,u.username,u.name,e.grade FROM enrollments e JOIN users u ON u.id=e.student_id WHERE e.offering_id=? ORDER BY u.id",{id});
    }));
    server.Put(R"(/api/offerings/(\d+)/grades)",api([](Db& db,const auto& req,const json& u){
        role(u,"PROFESSOR");auto data=body(req).at("grades");require(data.is_array()&&data.size()<=10,400,"成绩列表无效");Transaction tx(db);auto id=req.matches[1].str();
        auto rows=db.query("SELECT s.status FROM offerings o JOIN semesters s ON s.id=o.semester_id WHERE o.id=? AND o.professor_id=?",{id,uid(u)});require(!rows.empty(),403,"只能录入本人授课班级的成绩");require(rows[0]["status"]=="COMPLETED",409,"只能录入已结束学期的成绩");
        for(const auto& g:data){auto student=id_string(g.at("student_id"));auto grade=string_field(g,"grade",1);require(domain::valid_grade(grade),400,"成绩只能为 A/B/C/D/F/I");require(!db.query("SELECT student_id FROM enrollments WHERE offering_id=? AND student_id=?",{id,student}).empty(),400,"学生不在该班级");db.query("UPDATE enrollments SET grade=? WHERE offering_id=? AND student_id=?",{grade,id,student});}
        audit(db,u,"SUBMIT_GRADES",{{"offering_id",id},{"count",data.size()}});tx.commit();return json{{"ok",true}};
    }));
    server.Get("/api/admin/users",api([](Db& db,const auto&,const json& u){role(u,"REGISTRAR");auto rows=db.query("SELECT id,username,role,name,department,birth_date,graduation_date,ssn FROM users WHERE active=1 ORDER BY role,id");for(auto& r:rows){r["qualifications"]=json::array();if(r["role"]=="PROFESSOR")for(const auto& q:db.query("SELECT course_id FROM teaching_qualifications WHERE professor_id=?",{uid(r)}))r["qualifications"].push_back(q["course_id"]);}return rows;}));
    auto maintain_user=[](bool create){return api([create](Db& db,const auto& req,const json& u){
        role(u,"REGISTRAR");auto d=body(req);auto name=string_field(d,"name",80),dept=string_field(d,"department",80),birth=string_field(d,"birth_date",10);
        require(valid_date(birth),400,"出生日期无效");auto grad=d.value("graduation_date",std::string());auto ssn=d.value("ssn",std::string());require(ssn.size()<=32,400,"证件号过长");
        auto username=string_field(d,"username");
        require(std::all_of(username.begin(),username.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),400,"用户名只能包含英文字母、数字、下划线和短横线");
        Transaction tx(db);std::string id,kind;
        const auto user_id=create?std::string("0"):req.matches[1].str();require(db.query("SELECT id FROM users WHERE username=? AND id<>?",{username,user_id}).empty(),409,"用户名已存在");
        if(!ssn.empty()) require(db.query("SELECT id FROM users WHERE ssn=? AND id<>?",{ssn,user_id}).empty(),409,"身份编号已存在");
        if(create){kind=string_field(d,"role",16);require(kind=="STUDENT"||kind=="PROFESSOR",400,"只能创建学生或教师");auto username=string_field(d,"username",64),password=string_field(d,"password",128);require(password.size()>=10,400,"密码至少 10 位");require(std::all_of(username.begin(),username.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}),400,"用户名只能包含英文字母、数字、下划线和短横线");require(db.query("SELECT id FROM users WHERE username=?",{username}).empty(),409,"用户名已存在");auto salt=random_hex(16);if(kind=="STUDENT")require(valid_date(grad),400,"毕业日期无效");
            db.query("INSERT INTO users(username,password_hash,password_salt,role,name,department,birth_date,graduation_date,ssn) VALUES(?,?,?,?,?,?,?,NULLIF(?,''),NULLIF(?,''))",{username,password_hash(password,salt),salt,kind,name,dept,birth,grad,ssn});id=std::to_string(db.insert_id());
        }else{id=req.matches[1].str();auto rows=db.query("SELECT role FROM users WHERE id=?",{id});require(!rows.empty(),404,"用户不存在");kind=rows[0]["role"];require(kind!="REGISTRAR",403,"此入口只维护学生和教师");if(kind=="STUDENT")require(valid_date(grad),400,"毕业日期无效");db.query("UPDATE users SET username=?,name=?,department=?,birth_date=?,graduation_date=NULLIF(?,''),ssn=IF(?='',ssn,?) WHERE id=?",{username,name,dept,birth,grad,ssn,ssn,id});}
        if(!create && d.contains("password") && !d["password"].template get<std::string>().empty()) {
            auto password=string_field(d,"password",128); require(password.size()>=10,400,"密码至少 10 位"); auto salt=random_hex(16);
            db.query("UPDATE users SET password_hash=?,password_salt=? WHERE id=?",{password_hash(password,salt),salt,id});
            db.query("DELETE FROM sessions WHERE user_id=?",{id});
        }
        if(kind=="PROFESSOR"&&d.contains("qualifications")){auto qs=d["qualifications"];require(qs.is_array(),400,"授课资格必须为数组");std::set<int> ids;auto cs=catalog(db);for(const auto& q:qs){id_string(q);require(ids.insert(q.template get<int>()).second,400,"授课资格重复");bool exists=false;for(const auto& c:cs)if(c["id"]==q)exists=true;require(exists,400,"课程不存在");}for(const auto& o:db.query("SELECT o.course_id FROM offerings o JOIN semesters s ON s.id=o.semester_id WHERE o.professor_id=? AND s.status<>'COMPLETED'",{id}))require(ids.count(o["course_id"].template get<int>()),409,"不能撤销正在授课的课程资格");db.query("DELETE FROM teaching_qualifications WHERE professor_id=?",{id});for(int q:ids)db.query("INSERT INTO teaching_qualifications(professor_id,course_id) VALUES(?,?)",{id,std::to_string(q)});}
        audit(db,u,create?"CREATE_USER":"UPDATE_USER",{{"user_id",id}});tx.commit();return json{{"id",std::stoi(id)}};
    });};
    server.Post("/api/admin/users",maintain_user(true));server.Put(R"(/api/admin/users/(\d+))",maintain_user(false));
    server.Delete(R"(/api/admin/users/(\d+))",api([](Db& db,const auto& req,const json& u){
        role(u,"REGISTRAR");Transaction tx(db);auto id=req.matches[1].str();auto rows=db.query("SELECT role FROM users WHERE id=?",{id});require(!rows.empty(),404,"用户不存在");require(rows[0]["role"]!="REGISTRAR",403,"不能删除教务管理员");
        auto t=current_term(db);auto sid=std::to_string(t["id"].template get<int>());
        require(db.query("SELECT id FROM offerings WHERE professor_id=? AND semester_id=?",{id,sid}).empty(),409,"请先取消该教师本学期的授课安排");
        require(db.query("SELECT e.student_id FROM enrollments e JOIN offerings o ON o.id=e.offering_id WHERE e.student_id=? AND o.semester_id=?",{id,sid}).empty(),409,"请先取消该学生本学期的选课");
        db.query("UPDATE users SET active=0 WHERE id=?",{id});db.query("DELETE FROM sessions WHERE user_id=?",{id});audit(db,u,"DELETE_USER",{{"user_id",id}});tx.commit();return json{{"ok",true}};
    }));
    server.Get("/api/admin/overview",api([](Db& db,const auto&,const json& u){role(u,"REGISTRAR");auto t=current_term(db);return json{{"semester",t},{"users",db.query("SELECT role,COUNT(*) AS count FROM users WHERE active=1 GROUP BY role")},{"offerings",offerings(db,t["id"])},{"billing",db.query("SELECT status,COUNT(*) AS count,COALESCE(SUM(amount_cents),0) AS amount_cents FROM billing_outbox WHERE semester_id=? GROUP BY status",{std::to_string(t["id"].template get<int>())})},{"logs",db.query("SELECT a.id,a.action,a.created_at,u.name AS actor FROM audit_logs a JOIN users u ON u.id=a.actor_id ORDER BY a.id DESC LIMIT 15")},{"demo_billing_available",demo_billing_available.load()}};}));
    server.Get("/api/admin/billing",api([](Db& db,const auto&,const json& u){role(u,"REGISTRAR");auto t=current_term(db);return db.query("SELECT b.id,b.event_key,b.amount_cents,b.status,b.attempts,b.last_error,b.next_attempt_at,u.name,u.username FROM billing_outbox b JOIN users u ON u.id=b.student_id WHERE b.semester_id=? ORDER BY b.id DESC",{std::to_string(t["id"].template get<int>())});}));
    server.Post("/api/admin/billing/retry",api([](Db& db,const auto&,const json& u){role(u,"REGISTRAR");db.query("UPDATE billing_outbox SET next_attempt_at=UTC_TIMESTAMP() WHERE status='PENDING'");audit(db,u,"RETRY_BILLING",json::object());return json{{"ok",true}};}));
    server.Post("/api/admin/billing/availability",api([](Db& db,const auto& req,const json& u){role(u,"REGISTRAR");require(env("DEMO_BILLING")=="1",404,"演示功能未启用");bool available=body(req).at("available").template get<bool>();demo_billing_available=available;audit(db,u,"DEMO_BILLING_AVAILABILITY",{{"available",available}});return json{{"ok",true}};}));
    server.Post("/api/admin/close-registration",api([](Db& db,const auto&,const json& u){
        role(u,"REGISTRAR");Transaction tx(db,0);auto t=current_term(db);require(t["status"]=="OPEN",409,"选课已经关闭");auto sid=std::to_string(t["id"].template get<int>());auto all=offerings(db,t["id"]);json schedules=json::array();std::map<int,std::set<int>> passed;
        for(const auto& row:db.query("SELECT student_id,submitted_alternates FROM schedules WHERE semester_id=? ORDER BY student_id",{sid})){
            json s{{"student_id",row["student_id"]},{"primary",json::array()},{"alternates",json::parse(row["submitted_alternates"].template get<std::string>())}};
            for(const auto& r:db.query("SELECT e.offering_id FROM enrollments e JOIN offerings o ON o.id=e.offering_id WHERE e.student_id=? AND o.semester_id=? ORDER BY o.id",{std::to_string(row["student_id"].template get<int>()),sid}))s["primary"].push_back(r["offering_id"]);
            // Only an actually submitted schedule participates; a new draft never occupies a seat.
            if(!s["primary"].empty()){passed[row["student_id"].template get<int>()]=passed_courses(db,row["student_id"]);schedules.push_back(s);}
        }
        auto events=domain::level(all,schedules,passed);
        db.query("DELETE e FROM enrollments e JOIN offerings o ON o.id=e.offering_id WHERE o.semester_id=?",{sid});
        for(const auto& o:all)db.query("UPDATE offerings SET status=? WHERE id=?",{o["status"],std::to_string(o["id"].template get<int>())});
        for(const auto& s:schedules){auto student=std::to_string(s["student_id"].template get<int>());long long cents=0;json detail=json::array();
            for(const auto& id:s["primary"]){auto* o=find_offering(all,id.template get<int>());db.query("INSERT INTO enrollments(student_id,offering_id) VALUES(?,?)",{student,id_string(id)});cents+=(*o)["fee_cents"].template get<long long>();detail.push_back({{"offering_id",id},{"title",(*o)["title"]},{"fee_cents",(*o)["fee_cents"]}});}
            db.query("UPDATE schedules SET primary_choices=?,status='FINAL' WHERE student_id=? AND semester_id=?",{s["primary"].dump(),student,sid});
            auto key="tuition-"+sid+"-"+student;json payload{{"event_key",key},{"student_id",s["student_id"]},{"semester_id",t["id"]},{"amount_cents",cents},{"schedule",detail}};
            db.query("INSERT INTO billing_outbox(event_key,student_id,semester_id,amount_cents,payload) VALUES(?,?,?,?,?)",{key,student,sid,std::to_string(cents),payload.dump()});
        }
        db.query("UPDATE semesters SET status='CLOSED' WHERE id=?",{sid});audit(db,u,"CLOSE_REGISTRATION",events);tx.commit();return json{{"ok",true},{"events",events},{"bills",schedules.size()}};
    }));
    server.Post("/internal/demo-billing",api([](Db& db,const auto& req,const auto&){
        require(env("DEMO_BILLING")=="1",404,"Not found");auto key=env("BILLING_KEY");require(!key.empty()&&constant_equal(req.get_header_value("X-Billing-Key"),key),403,"Forbidden");require(demo_billing_available,503,"Demo finance system unavailable");auto d=body(req);auto event=string_field(d,"event_key",100);db.query("INSERT INTO billing_receipts(event_key,payload) VALUES(?,?) ON DUPLICATE KEY UPDATE event_key=VALUES(event_key)",{event,d.dump()});return json{{"accepted",true},{"event_key",event}};
    },false));
}
void billing_loop(std::atomic<bool>& stop) {
    mysql_thread_init();
    while(!stop){
        try {
            Db db;auto lock=db.query("SELECT GET_LOCK('course-registration-billing',0) AS acquired");
            if(lock[0]["acquired"]==1){
                for(const auto& row:db.query("SELECT * FROM billing_outbox WHERE status='PENDING' AND next_attempt_at<=UTC_TIMESTAMP() ORDER BY id LIMIT 10")){
                    auto url=env("BILLING_URL","http://127.0.0.1:8080/internal/demo-billing");auto scheme=url.find("://");require(scheme!=std::string::npos,500,"Invalid billing URL");auto slash=url.find('/',scheme+3);auto origin=url.substr(0,slash);auto path=slash==std::string::npos?"/":url.substr(slash);
                    httplib::Client client(origin);client.set_connection_timeout(2,0);client.set_read_timeout(3,0);client.set_write_timeout(3,0);
                    auto response=client.Post(path,{{"X-Billing-Key",env("BILLING_KEY")}},row["payload"].template get<std::string>(),"application/json");
                    bool ok=response&&response->status>=200&&response->status<300;
                    if(ok)db.query("UPDATE billing_outbox SET status='DELIVERED',attempts=attempts+1,last_error=NULL,delivered_at=UTC_TIMESTAMP() WHERE id=?",{uid(row)});
                    else {int backoff=std::min(300,2*(1<<std::min(7,row["attempts"].template get<int>())));db.query("UPDATE billing_outbox SET attempts=attempts+1,last_error=?,next_attempt_at=DATE_ADD(UTC_TIMESTAMP(),INTERVAL ? SECOND) WHERE id=?",{response?"HTTP "+std::to_string(response->status):"Connection unavailable",std::to_string(backoff),uid(row)});}
                }
                db.query("SELECT RELEASE_LOCK('course-registration-billing')");
            }
        } catch(const std::exception& e){std::cerr<<"Billing retry: "<<e.what()<<std::endl;}
        for(int i=0;i<10&&!stop;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    mysql_thread_end();
}
