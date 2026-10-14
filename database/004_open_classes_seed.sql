-- Seed three additional submitted students so four classes can open without
-- manual input. A student01 demonstration can make the fifth class open.
SET NAMES utf8mb4;
USE registration;
START TRANSACTION;

-- CS101 and CS201 are the prerequisites of the selected computing courses.
INSERT INTO enrollments (student_id, offering_id, grade) VALUES
 (2005,101,'A'),(2005,102,'B'),
 (2006,101,'A'),(2006,102,'B'),
 (2007,101,'A'),(2007,102,'B');

-- 201/202/203 already have student02 and student03; 205 has a teacher and
-- reaches the three-student minimum through these new registrations.
INSERT INTO schedules
 (student_id,semester_id,primary_choices,alternate_choices,submitted_alternates,status) VALUES
 (2005,2,JSON_ARRAY(201,202,203,205),JSON_ARRAY(206,208),JSON_ARRAY(206,208),'SUBMITTED'),
 (2006,2,JSON_ARRAY(201,202,203,205),JSON_ARRAY(206,208),JSON_ARRAY(206,208),'SUBMITTED'),
 (2007,2,JSON_ARRAY(201,202,203,205),JSON_ARRAY(206,208),JSON_ARRAY(206,208),'SUBMITTED');

INSERT INTO enrollments (student_id,offering_id) VALUES
 (2005,201),(2005,202),(2005,203),(2005,205),
 (2006,201),(2006,202),(2006,203),(2006,205),
 (2007,201),(2007,202),(2007,203),(2007,205);

-- An unassigned section overlaps professor prof01's software engineering
-- class. It lets the teaching page demonstrate a visible timetable clash.
INSERT INTO offerings
 (id,semester_id,course_id,section,professor_id,day,start_slot,end_slot,room,capacity,status) VALUES
 (321,2,1,'02',NULL,1,1,2,'明理楼 102',10,'open');
COMMIT;
