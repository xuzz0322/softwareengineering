#pragma once
#include "json.hpp"
#include <set>
#include <map>
#include <string>
#include <vector>
namespace domain {
using nlohmann::json;
inline bool conflicts(const json& a, const json& b) {
    return a.at("day") == b.at("day") && a.at("start_slot").get<int>() <= b.at("end_slot").get<int>()
        && b.at("start_slot").get<int>() <= a.at("end_slot").get<int>();
}
inline bool passed(const std::string& grade) { return grade == "A" || grade == "B" || grade == "C" || grade == "D"; }
inline bool valid_grade(const std::string& grade) { return passed(grade) || grade == "F" || grade == "I"; }
inline std::string can_add(const json& offering, const json& chosen, const std::set<int>& passed_courses) {
    if (offering.at("status") != "open") return "课程未开放";
    if (offering.at("enrolled").get<int>() >= offering.at("capacity").get<int>()) return "课程名额已满";
    for (const auto& pre : offering.at("prerequisites"))
        if (!passed_courses.count(pre.get<int>())) return "未满足先修课程要求";
    for (const auto& other : chosen) {
        if (other.at("course_id") == offering.at("course_id")) return "不能重复修读同一门课程";
        if (conflicts(other, offering)) return "课程时间冲突";
    }
    return "";
}
// Stable student ordering makes capacity allocation reproducible and auditable.
// Low-enrollment sections remain provisional until the final cancellation pass.
inline json level(json& offerings, json& schedules, const std::map<int, std::set<int>>& passed_by_student) {
    json changes = json::array();
    auto find = [&](int id) -> json* { for (auto& o : offerings) if (o["id"] == id) return &o; return nullptr; };
    for (auto& o : offerings) if (o["professor_id"].is_null()) o["status"] = "cancelled";
    for (auto& s : schedules) {
        json chosen = json::array(), kept = json::array();
        for (const auto& id : s["primary"]) {
            auto* o = find(id.get<int>());
            if (o && (*o)["status"] == "open") { kept.push_back(id); chosen.push_back(*o); }
            else changes.push_back({{"student_id",s["student_id"]},{"offering_id",id},{"action","cancelled"}});
        }
        auto it = passed_by_student.find(s["student_id"].get<int>());
        const std::set<int> empty;
        const auto& passed_courses = it == passed_by_student.end() ? empty : it->second;
        for (const auto& id : s["alternates"]) {
            if (kept.size() == 4) break;
            auto* o = find(id.get<int>());
            if (o && !(*o)["professor_id"].is_null() && can_add(*o, chosen, passed_courses).empty()) {
                kept.push_back(id); chosen.push_back(*o);
                (*o)["enrolled"] = (*o)["enrolled"].get<int>() + 1;
                changes.push_back({{"student_id",s["student_id"]},{"offering_id",id},{"action","alternate_added"}});
            }
        }
        s["primary"] = kept;
    }
    for (auto& o : offerings) {
        o["status"] = (o["professor_id"].is_null() || o["enrolled"].get<int>() < 3) ? "cancelled" : "closed";
    }
    for (auto& s : schedules) {
        json kept = json::array();
        for (const auto& id : s["primary"]) {
            auto* o = find(id.get<int>());
            if (o && (*o)["status"] == "closed") kept.push_back(id);
            else changes.push_back({{"student_id",s["student_id"]},{"offering_id",id},{"action","cancelled"}});
        }
        s["primary"] = kept;
    }
    return changes;
}
}
