SET NAMES utf8mb4;
USE legacy_catalog;
INSERT INTO legacy_catalog.courses (id,code,title,department,credits,fee_cents,prerequisites) VALUES
 (1,'CS101','程序设计基础','计算机学院',3,90000,JSON_ARRAY()),
 (2,'CS201','数据结构','计算机学院',4,120000,JSON_ARRAY(1)),
 (3,'CS301','软件工程','计算机学院',3,90000,JSON_ARRAY(2)),
 (4,'CS302','数据库系统','计算机学院',3,90000,JSON_ARRAY(1)),
 (5,'CS303','计算机网络','计算机学院',3,90000,JSON_ARRAY(1)),
 (6,'MA201','概率论与数理统计','数学学院',3,90000,JSON_ARRAY()),
 (7,'GE101','学术英语','外国语学院',2,60000,JSON_ARRAY()),
 (8,'CS304','操作系统','计算机学院',4,120000,JSON_ARRAY(2)),
 (9,'GE102','科技写作','人文学院',2,60000,JSON_ARRAY()),
 (10,'CS401','编译原理','计算机学院',4,120000,JSON_ARRAY(8));
USE registration;
-- Broad demonstration window; real semester dates are configured by the deployment owner.
INSERT INTO semesters (id,name,status,add_drop_start,add_drop_end) VALUES
 (1,'2026 春季学期','COMPLETED','2026-02-01','2026-03-01'),
 (2,'2026 秋季学期','OPEN','2026-01-01','2027-12-31');
INSERT INTO users (id,username,role,name,birth_date,ssn,department,graduation_date) VALUES
 (1,'admin','REGISTRAR','教务管理员',NULL,NULL,'教务处',NULL),
 (11,'prof01','PROFESSOR','陈思远','1980-05-12','DEMO-P001','计算机学院',NULL),
 (12,'prof02','PROFESSOR','林知行','1985-09-20','DEMO-P002','计算机学院',NULL),
 (13,'prof03','PROFESSOR','周若宁','1983-04-08','DEMO-P003','数学学院',NULL),
 (14,'prof04','PROFESSOR','许清和','1988-11-11','DEMO-P004','外国语学院',NULL),
 (101,'student01','STUDENT','林同学','2005-04-15','DEMO-S001','计算机学院','2027-06-30'),
 (102,'student02','STUDENT','陈同学','2005-06-21','DEMO-S002','计算机学院','2027-06-30'),
 (103,'student03','STUDENT','王同学','2005-10-02','DEMO-S003','计算机学院','2027-06-30'),
 (104,'student04','STUDENT','赵同学','2005-03-09','DEMO-S004','计算机学院','2027-06-30');
INSERT INTO teaching_qualifications (professor_id,course_id) VALUES (11,1),(11,2),(11,3),(11,4),(11,5),(11,8),(11,10),
 (12,1),(12,2),(12,3),(12,4),(12,5),(12,8),(12,10),(13,6),(14,7),(14,9);
INSERT INTO offerings (id,semester_id,course_id,section,professor_id,day,start_slot,end_slot,room,capacity,status) VALUES
 (101,1,1,'01',11,1,1,2,'明理楼 201',10,'closed'),
 (102,1,2,'01',12,2,1,2,'明理楼 202',10,'closed'),
 (201,2,3,'01',11,1,1,2,'明理楼 301',10,'open'),
 (202,2,4,'01',12,2,3,4,'知行楼 202',10,'open'),
 (203,2,5,'01',12,3,1,2,'明理楼 303',10,'open'),
 (204,2,6,'01',13,4,3,4,'致远楼 105',10,'open'),
 (205,2,7,'01',14,5,1,2,'博文楼 201',10,'open'),
 (206,2,8,'01',11,3,5,6,'明理楼 401',10,'open'),
 (207,2,9,'01',NULL,2,7,8,'博文楼 305',10,'open'),
 (208,2,10,'01',11,4,7,8,'知行楼 301',10,'open'),
 (209,2,4,'02',NULL,1,1,2,'知行楼 203',10,'open'),
 (210,2,1,'01',NULL,5,5,6,'明理楼 102',10,'open');
INSERT INTO enrollments(student_id,offering_id,grade) VALUES
 (101,101,'A'),(101,102,'B'),(102,101,'A'),(102,102,'A'),
 (103,101,'B'),(103,102,'B'),(104,101,'C'),(104,102,'I');
-- Two already registered students allow a third student to demonstrate minimum enrollment.
INSERT INTO schedules(student_id,semester_id,primary_choices,alternate_choices,submitted_alternates,status) VALUES
 (102,2,JSON_ARRAY(201,202,203,204),JSON_ARRAY(205,206),JSON_ARRAY(205,206),'SUBMITTED'),
 (103,2,JSON_ARRAY(201,202,203,204),JSON_ARRAY(205,206),JSON_ARRAY(205,206),'SUBMITTED');
INSERT INTO enrollments(student_id,offering_id) VALUES (102,201),(102,202),(102,203),(102,204),(103,201),(103,202),(103,203),(103,204);
