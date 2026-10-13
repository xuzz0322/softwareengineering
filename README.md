# 校园选课系统

按工作区《2026年软件工程课程设计题目(1).pdf》和《2026年软件工程课程设计要求(1).ppt》实现的课程设计工程。

**技术：C++17 后端 + HTML/CSS/JavaScript 前端 + MySQL 8.4 数据库。** 业务逻辑、鉴权、数据库访问和 HTTP 接口均由 C++ 实现；浏览器使用 JavaScript 完成交互。无 Node.js 运行时依赖。

## 快速运行

安装并启动 Docker Desktop（Windows/macOS）或 Docker Engine + Compose（Linux），然后在本目录执行：

```sh
sh scripts/setup.sh
docker compose up -d --build
```

浏览器打开 **http://localhost:8080**。首次构建需要联网下载 Debian、MySQL 镜像和编译工具，可能需要几分钟。

| 角色 | 账号                         | 初始演示密码    |
| ---- | ---------------------------- | --------------- |
| 学生 | `student01`～`student60` | `Campus2026!` |
| 教师 | `prof01`～`prof12`       | `Campus2026!` |
| 教务 | `admin`                    | `Campus2026!` |

`setup.sh` 生成仅本机保存的 `.env`，数据库密码和财务密钥随机生成。可在首次启动前修改 `DEMO_PASSWORD`。人员密码首次启动时使用 PBKDF2 加盐初始化；更改环境变量不会重置已有账户的密码。`.env` 已加入忽略规则，不要提交。

Windows 可在 Git Bash / WSL 中执行 `sh scripts/setup.sh`；也可手动复制 `.env.example` 为 `.env` 并填写各密码与密钥，再执行 Compose 命令。

```sh
docker compose ps                    # 检查服务是否 healthy
docker compose logs --tail=100 app   # 查看服务日志
docker compose down                 # 停止服务，保留数据库
```

网页端口冲突时，将 `.env` 的 `APP_PORT` 改为 `8088` 等未占用端口，重新启动，然后访问对应端口。

### 使用本机 Navicat 连接 MySQL

Compose 将数据库仅映射到本机回环地址，默认使用 **127.0.0.1:3307**。执行 `docker compose up -d --wait` 后，在 Navicat 新建 **MySQL** 连接：

| 项目       | 填写值                                                          |
| ---------- | --------------------------------------------------------------- |
| 主机       | `127.0.0.1`                                                   |
| 端口       | `3307`                                                        |
| 用户名     | `registration`                                                |
| 密码       | 工程目录`.env` 文件中的 `DB_PASSWORD` 值                    |
| 初始数据库 | `registration`（连接成功后也可查看只读的 `legacy_catalog`） |

密码是 `sh scripts/setup.sh` 首次运行时生成的本机随机值，不是网页演示账号密码。Navicat 选择普通 MySQL 连接，无需 SSH 隧道；先点击“测试连接”，再保存。若 3307 已被本机其他服务占用，可在 `.env` 中加入 `MYSQL_HOST_PORT=3308` 等空闲端口，重新运行 `docker compose up -d --wait`，并在 Navicat 使用相同端口。不要把 `.env` 或数据库密码发给他人。

**重新初始化演示数据库会删除本项目所有数据。** 只有明确需要重置演示数据时才执行 `docker compose down -v` 后重新启动。平时不要使用 `-v`。

## 演示数据规模

包含 30 门课程、30 个本学期教学班、60 名学生、12 名教授和 1 名教务管理员。新增课程覆盖计算机、数学、外语、经管、人文和艺术；新增教授已配置授课资格及无自身时间冲突的教学班。

新增学生尚未选课、没有预置先修成绩，可先选无先修要求的课程。原 student01–student04 保留历史成绩，便于演示先修检查。演示人员均为虚构数据。

首次启动自动执行 database/003_expanded_seed.sql。已有数据库可增量导入该文件后重启应用；脚本不覆盖已有记录，新账号密码由应用启动时初始化为 DEMO_PASSWORD。

## 功能

- 学生：按学院、关键词和空余名额筛选课程，查看先修提示与实际报名状态，保存草稿，提交/调整/删除课表，选择 4 门主选和 2 门备选，查看历史成绩。
- 界面：蓝色与暖橙的校园工作台主题，统一登录页、角色导航、概览卡片、课程目录、数据表格和弹窗；适配桌面与手机窄屏，并保留键盘焦点提示和减少动画偏好。
- 教师：在具备授课资格的课程中认领教学班，检查授课冲突，查看本人班级名单，录入/更正已结束学期成绩。
- 教务：学生与教师增删改、教师资格维护、教学班人数检查、关闭选课、计费查询和重试。
- 业务：先修课校验、时间冲突校验、同一课程不能重复选班、容量 10 人、最少 3 人开班、无教师取消、备选顺序补位。
- 保护措施：参数化 SQL、密码加盐哈希、HttpOnly 会话 Cookie、CSRF 令牌、角色及资源归属检查、登录限速、成绩接口禁止缓存。
- 事务：提交失败完整回滚，跨服务进程数据库锁防止超额，选课事务进行中拒绝关闭。
- 财务：持久化 outbox、失败自动退避重试、收款端幂等键防重复入账。

## 权限限制

- 学生只能查看和修改自己的课表；伪造其他学生编号返回 403。
- 教授只能调整本人负责的教学班，或认领未分配且有资格的班级。
- 只有该教学班的教授可以录入已结束学期成绩，学生和教务均不可录入。
- 只有教务注册员（Registrar）可以新增、修改或停用学生资料。

以上规则由 C++ 后端验证，隐藏页面按钮不能替代权限校验。

## 推荐演示顺序

1. 以 `student01` 登录，查看课程目录。选择软件工程、数据库系统、计算机网络、概率论作为主选；学术英语、操作系统作为备选。保存草稿，再提交。
2. 修改主选使其包含编译原理，可看到“未满足先修课程要求”；选择软件工程和数据库系统 02 班，可看到时间冲突。失败后原课表不变。
3. 以 `prof01` 登录，查看软件工程学生名单；进入“成绩录入”，修改春季程序设计基础成绩。
4. 回到 `student01` 的“学期成绩”，验证成绩更新。
5. 以 `admin` 登录，维护人员，再检查选课情况。关闭选课后四个教学班达到 3 人可开班，其他班级取消。
6. 查看“计费记录”。最终每位已提交课表学生生成一笔计费；学生返回课表后无法继续选退课。

为了演示备选补位，可在步骤 1 将概率论换成没有教师的科技写作，并把概率论设为第一备选。关闭后科技写作取消、概率论补入。

## 工程结构

```text
backend/
  include/db.hpp       数据库、事务与密码接口
  include/domain.hpp   冲突/先修检查和关闭选课补位算法
  src/db.cpp           MySQL 预处理语句封装、数据库锁、PBKDF2
  src/service.cpp      REST API、鉴权、应用业务与计费工作线程
  src/main.cpp         C++ 服务启动入口
  vendor/              固定版本 HTTP 和 JSON 单头文件库
frontend/              中文响应式网页，三种角色工作台
 database/             MySQL 表结构、只读课程目录、演示数据
 docs/                 开发计划、需求、设计、测试、总结与核验记录
 tests/                C++ 单元测试、真实 MySQL API 集成测试
 scripts/              配置生成、隔离测试脚本
```

## 测试

```sh
sh scripts/test.sh
```

脚本使用 **`course-registration-tests` 独立 Compose 项目、独立卷和 18080 端口**；会重置并删除这个测试项目的数据，不操作 `course-registration` 演示数据库。结果写入 `output/integration-results.txt`，失败则以非零状态退出。若修改了演示密码，设置 `TEST_DEMO_PASSWORD` 为实际值后运行。

测试数据库使用独立的本机 13307 端口，不与演示数据库的 3307 端口冲突。

测试包括数据库真实读写、角色隔离、错误回滚、12 人并发抢位、关闭期间锁定、补位、取消课程、计费断连恢复与幂等重放。C++ 规则单元测试在 Docker 构建时自动执行。

## 开班演示数据

全新数据库会自动执行 `database/004_open_classes_seed.sql`。学期 2 中，student02、student03、student05～student07 已正式提交课表；软件工程、数据库系统、计算机网络各有 5 人，学术英语有 3 人，均达到最低开班人数。概率论原有 2 人，若 student01 提交主选 201/202/203/207、备选 204/205，关闭时无教师的科技写作被取消，第一备选概率论补入后达到 3 人，最终共有 5 个教学班开课。不提交 student01 时也有 4 个班达到条件。新增学生的历史先修成绩、课程时间和授课教师均与这些报名匹配。

已有数据库的初始化 SQL 不会自动重跑。如果学期 2 已关闭，可在确认尚无学期 3 后执行一次 `scripts/prepare-next-demo-term.sql`，它新增开放的“2026 秋季补充演示学期”，保留原学期的最终课表和账单：

```sh
docker compose exec -T db sh -c 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --default-character-set=utf8mb4 registration' < scripts/prepare-next-demo-term.sql
```

新学期的 401～405 号班次沿用上述安排：当前 401/402/403 各 5 人、405 为 3 人；student01 可主选 401/402/403/407、备选 404/405，关课后 404 也达到 3 人。请在其他页面演示结束后再关闭选课；关闭动作不能撤销。

前端状态与筛选检查：

```sh
node tests/frontend_smoke.cjs
```

本机也可单独运行不依赖 MySQL 的规则测试：

```sh
c++ -std=c++17 -Ibackend/include -Ibackend/vendor tests/domain_tests.cpp -o /tmp/domain_tests
/tmp/domain_tests
```

## 原生编译

需要 CMake ≥3.20、C++17 编译器、pkg-config、OpenSSL 开发包，以及 MySQL Connector/C 或兼容 MySQL 协议的 MariaDB Connector/C 开发包。**连接器不是数据库服务端；运行和验证的数据库是 MySQL 8.4。**

```sh
cmake -S . -谭赛 build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

创建 `registration` 数据库、应用账号，将 database 下四个 SQL 文件按编号导入；为应用账号授予 `registration.*` 的 CRUD 权限和 `legacy_catalog.*` 的 SELECT 权限。运行前设置 `DB_HOST`、`DB_PORT`、`DB_USER`、`DB_PASSWORD`、`DB_NAME`、`DEMO_PASSWORD`、`BILLING_KEY`，在工程根目录运行 `./build/registration`。默认仅监听 `127.0.0.1:8080`。

## 原题适配与边界

- 原 PDF 提到 Windows 95/98 桌面界面；要求 PPT 允许 Web 实现。本工程采用现代浏览器界面，不宣称兼容 Windows 95/98。
- 原题的 Ingres/DEC VAX 目录没有提供连接方式。本工程以 **MySQL 独立 `legacy_catalog` 库**模拟遗留目录，应用只有 SELECT 权限，缓存 60 秒。更换真实 Ingres 时需要独立接入 ODBC/SQL 适配器；目前没有真实 Ingres 联调结果。
- 未提供实际财务系统。本地 C++ 演示接收端写入 `billing_receipts`，不进行真实支付。生产对接应配置 `BILLING_URL` / `BILLING_KEY`，财务端遵守 `event_key` 幂等约定；设置 `DEMO_BILLING=0` 禁用演示入口。
- 当前教学时间用工作日和节次建模，同一学期教学周视为重合。学费以整数“分”保存。
- 学期时间和课程目录由部署方通过 SQL 维护；题目没有要求课程目录写入界面。演示开放期特意设宽，真实部署请修改为实际选退课日期。
- 实际有正式报名的课表才参与关闭；新建未提交草稿不计费。保存修改草稿保留原报名和上次提交的备选。关闭时先取消无教师班级，保留有教师的临时班级供补位，再最终取消不足 3 人的班级。最终取消后不再次迭代补位，详见设计文档中原题步骤解释。
- 暂未达到或验证“2000 中央并发用户、500 本地并发用户、24×7 可用性”等生产指标。当前全局选课写锁适合课程设计的确定性演示，大规模部署需连接池、行锁改造、性能和故障测试。
- 对外部署需要 HTTPS 反向代理并配置 `COOKIE_SECURE=1`；当前 Compose 仅在本机回环地址暴露网页。

## 课程文档

[四人职责与交接表](docs/10-四人职责与交接表.md)：常鹂沣 需求与人员后端；谭赛 鉴权与选课教学；吴宪光 前端与界面验收；徐振泽 数据库、关闭计费与部署。各人维护本模块测试，徐振泽 组织全量回归。

见 [文档清单与填写说明](docs/00-文档清单与填写说明.md)，包含四份独立个人总结和组长汇总。

1. [开发计划](docs/01-开发计划.md)
2. [需求分析](docs/02-需求分析.md)
3. [设计文档](docs/03-设计文档.md)
4. [测试计划](docs/04-测试计划.md)
5. [测试报告](docs/05-测试报告.md)
6. [工作总结及个人填写模板](docs/06-工作总结.md)
7. [配置管理计划](docs/07-配置管理计划.md)
8. [验收检查清单](docs/08-验收检查清单.md)
9. [接口说明](docs/09-接口说明.md)
10. [AI 辅助使用说明](docs/11-AI辅助使用说明.md)

文档按四位成员的姓名标明岗位；实际工作量以组内开发记录为准。
