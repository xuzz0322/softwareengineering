SET NAMES utf8mb4;
-- MySQL 8.4. Import as a schema owner, not as the application account.
CREATE DATABASE IF NOT EXISTS legacy_catalog CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE legacy_catalog;
CREATE TABLE courses (
 id INT PRIMARY KEY,
 code VARCHAR(20) NOT NULL UNIQUE,
 title VARCHAR(100) NOT NULL,
 department VARCHAR(100) NOT NULL,
 credits INT NOT NULL CHECK (credits BETWEEN 1 AND 10),
 fee_cents INT NOT NULL CHECK (fee_cents >= 0),
 prerequisites JSON NOT NULL
);
USE registration;
CREATE TABLE semesters (
 id INT PRIMARY KEY,
 name VARCHAR(100) NOT NULL,
 status ENUM('OPEN','CLOSED','COMPLETED') NOT NULL,
 add_drop_start DATE NOT NULL,
 add_drop_end DATE NOT NULL,
 CHECK (add_drop_start <= add_drop_end)
);
CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(64) NOT NULL UNIQUE,
    role ENUM('STUDENT','PROFESSOR','REGISTRAR') NOT NULL,
    name VARCHAR(80) NOT NULL,
    birth_date DATE NULL,
    ssn VARCHAR(64) NULL UNIQUE,
    department VARCHAR(80) NOT NULL,
    graduation_date DATE NULL,
    password_hash VARCHAR(128) NULL,
    password_salt VARCHAR(64) NULL,
    active BOOLEAN NOT NULL DEFAULT TRUE
);
CREATE TABLE teaching_qualifications (
 professor_id INT NOT NULL,
 course_id INT NOT NULL,
 PRIMARY KEY (professor_id,course_id),
 FOREIGN KEY (professor_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY (course_id) REFERENCES legacy_catalog.courses(id)
);
CREATE TABLE offerings (
 id INT PRIMARY KEY,
 semester_id INT NOT NULL,
 course_id INT NOT NULL,
 section VARCHAR(10) NOT NULL,
 professor_id INT NULL,
 day INT NOT NULL CHECK (day BETWEEN 1 AND 5),
 start_slot INT NOT NULL CHECK (start_slot BETWEEN 1 AND 12),
 end_slot INT NOT NULL CHECK (end_slot BETWEEN 1 AND 12),
 room VARCHAR(100) NOT NULL,
 capacity INT NOT NULL DEFAULT 10 CHECK (capacity BETWEEN 3 AND 10),
 status ENUM('open','closed','cancelled') NOT NULL DEFAULT 'open',
 UNIQUE (semester_id,course_id,section),
 CHECK (start_slot <= end_slot),
 FOREIGN KEY (semester_id) REFERENCES semesters(id),
 FOREIGN KEY (course_id) REFERENCES legacy_catalog.courses(id),
 FOREIGN KEY (professor_id) REFERENCES users(id) ON DELETE SET NULL
);
CREATE TABLE enrollments (
 student_id INT NOT NULL,
 offering_id INT NOT NULL,
 grade ENUM('A','B','C','D','F','I') NULL,
 PRIMARY KEY (student_id,offering_id),
 FOREIGN KEY (student_id) REFERENCES users(id) ON DELETE CASCADE,
 FOREIGN KEY (offering_id) REFERENCES offerings(id)
);
CREATE TABLE schedules (
    student_id INT NOT NULL,
    semester_id INT NOT NULL,
    primary_choices JSON NOT NULL,
    alternate_choices JSON NOT NULL,
    submitted_alternates JSON NOT NULL,
    status ENUM('DRAFT','SUBMITTED','FINAL') NOT NULL DEFAULT 'DRAFT',
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (student_id,semester_id),
    FOREIGN KEY (student_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (semester_id) REFERENCES semesters(id)
);
CREATE TABLE sessions (
 token_hash CHAR(64) PRIMARY KEY,
 user_id INT NOT NULL,
 csrf_token VARCHAR(64) NOT NULL,
 expires_at TIMESTAMP NOT NULL,
 INDEX (expires_at),
 FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);
CREATE TABLE billing_outbox (
 id INT AUTO_INCREMENT PRIMARY KEY,
 event_key VARCHAR(100) NOT NULL UNIQUE,
 student_id INT NOT NULL,
 semester_id INT NOT NULL,
 amount_cents INT NOT NULL CHECK (amount_cents >= 0),
 payload JSON NOT NULL,
 status ENUM('PENDING','DELIVERED') NOT NULL DEFAULT 'PENDING',
 attempts INT NOT NULL DEFAULT 0,
 next_attempt_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 last_error VARCHAR(255) NULL,
 delivered_at DATETIME NULL,
 created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 INDEX (status,next_attempt_at),
 UNIQUE (student_id,semester_id)
);
-- Separate ledger representing a finance-system receiver. Preserve it independently of personnel deletion.
CREATE TABLE billing_receipts (
 event_key VARCHAR(100) PRIMARY KEY,
 payload JSON NOT NULL,
 received_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE audit_logs (
 id INT AUTO_INCREMENT PRIMARY KEY,
 actor_id INT NULL,
 action VARCHAR(50) NOT NULL,
 detail JSON NOT NULL,
 created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
 FOREIGN KEY (actor_id) REFERENCES users(id) ON DELETE SET NULL
);
-- The legacy directory cannot be written by the application.
GRANT SELECT ON legacy_catalog.* TO 'registration'@'%';
