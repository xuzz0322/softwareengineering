-- Use only when semester 2 has already closed. Keeps its final schedules and
-- billing records intact, and opens a separate autumn demonstration term.
SET NAMES utf8mb4;
USE registration;
START TRANSACTION;

INSERT INTO semesters (id,name,status,add_drop_start,add_drop_end) VALUES
 (3,'2026 秋季补充演示学期','OPEN','2026-10-01','2026-12-31');

-- These instructors hold the matching teaching qualifications in the seed.
-- Times are distinct across each student's four primary choices.
INSERT INTO offerings
 (id,semester_id,course_id,section,professor_id,day,start_slot,end_slot,room,capacity,status) VALUES
 (401,3,3,'01',11,1,1,2,'明理楼 301',10,'open'),
 (402,3,4,'01',12,2,3,4,'知行楼 202',10,'open'),
 (403,3,5,'01',12,3,1,2,'明理楼 303',10,'open'),
 (404,3,6,'01',13,4,3,4,'致远楼 105',10,'open'),
 (405,3,7,'01',14,5,1,2,'博文楼 201',10,'open'),
 (406,3,8,'01',11,3,5,6,'明理楼 401',10,'open'),
 (407,3,9,'01',NULL,2,7,8,'博文楼 305',10,'open'),
 (408,3,10,'01',11,4,7,8,'知行楼 301',10,'open'),
 (409,3,1,'02',NULL,1,1,2,'明理楼 102',10,'open');

-- Three added students have passed both CS101 and CS201. Existing students
-- 02 and 03 already have these grades in the completed first semester.
INSERT INTO enrollments (student_id,offering_id,grade) VALUES
 (2005,101,'A'),(2005,102,'B'),
 (2006,101,'A'),(2006,102,'B'),
 (2007,101,'A'),(2007,102,'B')
 ON DUPLICATE KEY UPDATE student_id=student_id;

INSERT INTO schedules
 (student_id,semester_id,primary_choices,alternate_choices,submitted_alternates,status) VALUES
 (102,3,JSON_ARRAY(401,402,403,404),JSON_ARRAY(405,406),JSON_ARRAY(405,406),'SUBMITTED'),
 (103,3,JSON_ARRAY(401,402,403,404),JSON_ARRAY(405,406),JSON_ARRAY(405,406),'SUBMITTED'),
 (2005,3,JSON_ARRAY(401,402,403,405),JSON_ARRAY(406,408),JSON_ARRAY(406,408),'SUBMITTED'),
 (2006,3,JSON_ARRAY(401,402,403,405),JSON_ARRAY(406,408),JSON_ARRAY(406,408),'SUBMITTED'),
 (2007,3,JSON_ARRAY(401,402,403,405),JSON_ARRAY(406,408),JSON_ARRAY(406,408),'SUBMITTED');

INSERT INTO enrollments (student_id,offering_id) VALUES
 (102,401),(102,402),(102,403),(102,404),
 (103,401),(103,402),(103,403),(103,404),
 (2005,401),(2005,402),(2005,403),(2005,405),
 (2006,401),(2006,402),(2006,403),(2006,405),
 (2007,401),(2007,402),(2007,403),(2007,405);
COMMIT;
