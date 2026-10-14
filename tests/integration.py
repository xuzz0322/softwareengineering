#!/usr/bin/env python3
"""Destructive integration tests, restricted to the disposable test Compose project."""
import concurrent.futures
import http.cookiejar
import json
import os
import subprocess
import time
import urllib.error
import urllib.request

PROJECT = 'course-registration-tests'
BASE = 'http://127.0.0.1:' + os.environ.get('APP_PORT', '18080')
PASSWORD = os.environ.get('TEST_DEMO_PASSWORD', 'Campus2026!')
COMPOSE = ['docker', 'compose', '-p', PROJECT]
results = []

def sql(text):
    return subprocess.check_output(COMPOSE + ['exec', '-T', 'db', 'sh', '-c',
        'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --default-character-set=utf8mb4 -N -B registration'],
        input=text.encode(), stderr=subprocess.STDOUT).decode().strip()

class Client:
    def __init__(self, username=None):
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        self.csrf = ''
        if username:
            code, data = self.call('/login', 'POST', {'username': username, 'password': PASSWORD})
            assert code == 200, (username, code, data)
            self.csrf = data['csrf']

    def call(self, path, method='GET', body=None, csrf=True):
        headers = {'Content-Type': 'application/json'}
        if csrf:
            headers['X-CSRF-Token'] = self.csrf
        req = urllib.request.Request(BASE + '/api' + path, method=method, headers=headers,
            data=None if body is None else json.dumps(body).encode())
        try:
            response = self.opener.open(req, timeout=15)
        except urllib.error.HTTPError as error:
            response = error
        return response.code, json.loads(response.read())

def check(name, condition):
    if not condition:
        raise AssertionError(name)
    results.append(name)
    print('PASS', name, flush=True)

valid = {'primary': [201, 202, 203, 204], 'alternates': [205, 206], 'submit': True}
start = time.perf_counter()
anon = Client()
check('health endpoint connects to MySQL', anon.call('/health')[0] == 200)
check('anonymous grade access denied', anon.call('/report-card')[0] == 401)
check('wrong password rejected', anon.call('/login', 'POST', {'username':'student01','password':'wrong'})[0] == 401)
s = Client('student01'); other = Client('student02'); p = Client('prof01'); a = Client('admin')
check('login cookie session returns student role', s.call('/me')[1]['user']['role'] == 'STUDENT')
check('missing CSRF token denied', s.call('/schedule', 'PUT', valid, csrf=False)[0] == 403)
check('students cannot maintain users', s.call('/admin/users')[0] == 403)
check('professor cannot read student report-card route', p.call('/report-card')[0] == 403)
check('report card contains only own completed grades', [row['grade'] for row in s.call('/report-card')[1]] == ['A','B'])
catalog = s.call('/offerings')[1]
offering201 = next(o for o in catalog['offerings'] if o['id']==201)
check('seed text is UTF-8 Chinese without mojibake', offering201['title'] == '软件工程' and offering201['department'] == '计算机学院' and offering201['room'] == '明理楼 301' and s.call('/me')[1]['user']['name'] == '林同学' and catalog['semester']['name'] == '2026 秋季学期')
check('expanded demo contains sixty students and twelve professors', sql("SELECT COUNT(*) FROM users WHERE role='STUDENT';") == '60' and sql("SELECT COUNT(*) FROM users WHERE role='PROFESSOR';") == '12')
check('seeded submissions match enrolled classes and passed prerequisites', sql("SELECT COUNT(*) FROM schedules WHERE semester_id=2 AND status='SUBMITTED';") == '5' and sql("SELECT COUNT(*) FROM enrollments WHERE student_id IN (2005,2006,2007) AND offering_id IN (201,202,203,205);") == '12' and sql("SELECT COUNT(*) FROM enrollments WHERE student_id IN (2005,2006,2007) AND offering_id IN (101,102) AND grade IN ('A','B','C','D');") == '6')
new_student = Client('student60'); new_professor = Client('prof12')
check('new demo accounts can log in and professor has assigned course', new_student.call('/me')[1]['user']['role'] == 'STUDENT' and len(new_professor.call('/teaching')[1]) == 1)
check('catalog includes prerequisites and teachers', len(s.call('/offerings')[1]['offerings']) == 31)
check('submitting fewer than four primary choices denied', s.call('/schedule','PUT',{'primary':[201], 'alternates':[], 'submit':True})[0] == 400)
check('missing prerequisite denied', s.call('/schedule','PUT',{'primary':[201,202,203,208], 'alternates':[204,205], 'submit':True})[0] == 409)
check('failed submission leaves no partial enrollment', sql('SELECT COUNT(*) FROM enrollments WHERE student_id=101 AND offering_id>=200;') == '0')
check('overlapping class times denied', s.call('/schedule','PUT',{'primary':[201,209,203,204], 'alternates':[205,206], 'submit':True})[0] == 409)
check('duplicate course sections denied', s.call('/schedule','PUT',{'primary':[201,202,203,204], 'alternates':[209,206], 'submit':True})[0] == 400)
check('draft can be saved without enrolling', s.call('/schedule','PUT',{'primary':[201,202], 'alternates':[], 'submit':False})[0] == 200 and sql('SELECT COUNT(*) FROM enrollments WHERE student_id=101 AND offering_id>=200;') == '0')
own_before = s.call('/schedule')[1]
other_before = other.call('/schedule')[1]
check('foreign schedule owner in body rejected', s.call('/schedule','PUT',{**valid,'student_id':102})[0] == 403)
check('foreign schedule owner in query rejected', s.call('/schedule?student_id=102','PUT',valid)[0] == 403)
check('foreign schedule delete body rejected', s.call('/schedule','DELETE',{'student_id':102})[0] == 403)
check('foreign schedule delete query rejected', s.call('/schedule?student_id=102','DELETE')[0] == 403)
check('foreign schedule read rejected', s.call('/schedule?student_id=102')[0] == 403)
check('rejected ownership attempts preserve both schedules', s.call('/schedule')[1] == own_before and other.call('/schedule')[1] == other_before)
check('professor and registrar cannot edit student schedule', p.call('/schedule','PUT',valid)[0] == 403 and a.call('/schedule','PUT',valid)[0] == 403)
check('valid four plus two schedule accepted', s.call('/schedule','PUT',valid)[0] == 200)
check('owner cannot be spoofed in request body', sql('SELECT COUNT(*) FROM enrollments WHERE student_id=101 AND offering_id>=200;') == '4' and other.call('/schedule')[1]['student_id'] == 102)
check('failed update rolls back previous schedule', s.call('/schedule','PUT',{'primary':[201,202,203,208], 'alternates':[204,205], 'submit':True})[0] == 409 and sql('SELECT COUNT(*) FROM enrollments WHERE student_id=101 AND offering_id>=200;') == '4')
check('deleting schedule releases seats', s.call('/schedule','DELETE')[0] == 200 and sql('SELECT COUNT(*) FROM enrollments WHERE student_id=101 AND offering_id>=200;') == '0')
check('deleting absent schedule gives 404', s.call('/schedule','DELETE')[0] == 404)
teaching_before = sql('SELECT id,professor_id FROM offerings ORDER BY id;')
check('professor cannot take another professor class', p.call('/teaching','PUT',{'offerings':[202]})[0] == 409)
check('foreign teaching attempt leaves all assignments unchanged', sql('SELECT id,professor_id FROM offerings ORDER BY id;') == teaching_before)
check('professor timetable conflicts denied atomically', p.call('/teaching','PUT',{'offerings':[201,206,208,209]})[0] == 409 and sql('SELECT professor_id FROM offerings WHERE id=201;') == '11')
check('eligible professor can select teaching', p.call('/teaching','PUT',{'offerings':[201,206,208,210]})[0] == 200)
check('professor can deselect teaching', p.call('/teaching','PUT',{'offerings':[201,206,208]})[0] == 200 and sql('SELECT professor_id IS NULL FROM offerings WHERE id=210;') == '1')
check('professor only sees own rosters', p.call('/offerings/202/roster')[0] == 403 and p.call('/offerings/201/roster')[0] == 200)
check('grades restricted to course owner', p.call('/offerings/102/grades','PUT',{'grades':[{'student_id':101,'grade':'A'}]})[0] == 403)
check('students cannot submit grades', s.call('/offerings/101/grades','PUT',{'grades':[{'student_id':101,'grade':'A'}]})[0] == 403)
check('registrar cannot submit grades', a.call('/offerings/101/grades','PUT',{'grades':[{'student_id':101,'grade':'F'}]})[0] == 403)
check('forged role cannot authorize student grading', s.call('/offerings/101/grades','PUT',{'role':'PROFESSOR','professor_id':11,'grades':[{'student_id':101,'grade':'F'}]})[0] == 403)
check('denied grade edits leave grades unchanged', sql('SELECT grade FROM enrollments WHERE student_id=101 AND offering_id=101;') == 'A' and sql('SELECT grade FROM enrollments WHERE student_id=101 AND offering_id=102;') == 'B')
check('current semester grades cannot be entered', p.call('/offerings/201/grades','PUT',{'grades':[{'student_id':102,'grade':'A'}]})[0] == 409)
check('invalid letter grade rejected', p.call('/offerings/101/grades','PUT',{'grades':[{'student_id':101,'grade':'Z'}]})[0] == 400)
check('partial grade update preserves skipped students', p.call('/offerings/101/grades','PUT',{'grades':[{'student_id':101,'grade':'B'}]})[0] == 200 and sql('SELECT grade FROM enrollments WHERE student_id=102 AND offering_id=101;') == 'A')
check('student sees corrected grade', s.call('/report-card')[1][0]['grade'] == 'B')
body = {'username':'temp_user', 'role':'STUDENT','name':"测试'; DROP TABLE users; --",'birth_date':'2005-01-01','ssn':'TEST-X','department':'计算机学院','graduation_date':'2027-06-30','password':PASSWORD}
person_before = sql('SELECT id,name,active FROM users ORDER BY id;')
for actor, label in [(s, 'student'), (p, 'professor')]:
    check(label+' cannot create student', actor.call('/admin/users','POST',body)[0] == 403)
    check(label+' cannot update student or forge registrar role', actor.call('/admin/users/101','PUT',{**body,'role':'REGISTRAR'})[0] == 403)
    check(label+' cannot delete student', actor.call('/admin/users/104','DELETE')[0] == 403)
check('denied personnel edits preserve all users', sql('SELECT id,name,active FROM users ORDER BY id;') == person_before)
code, created = a.call('/admin/users','POST',body)
check('registrar creates user with safely bound input', code == 200 and sql('SELECT COUNT(*) FROM users;') == '74')
uid = created['id']
check('registrar can update student', a.call('/admin/users/'+str(uid),'PUT',{**body,'name':'已修改'})[0] == 200)
check('invalid calendar date rejected', a.call('/admin/users/'+str(uid),'PUT',{**body,'birth_date':'2005-02-30'})[0] == 400)
check('duplicate identity number rejected', a.call('/admin/users','POST',{**body,'username':'duplicate_identity'})[0] == 409)
check('registrar can delete unused student', a.call('/admin/users/'+str(uid),'DELETE')[0] == 200)
check('deleted user cannot log in', anon.call('/login','POST',{'username':'temp_user','password':PASSWORD})[0] == 401)
check('professor with teaching assignments cannot be deleted', a.call('/admin/users/11','DELETE')[0] == 409)
# Twelve different students compete for the five remaining seats in classes
# 201-203. Class 204 has eight seats, but each submission is atomic.
for index in range(12):
    sql(f"INSERT INTO users(username,password_hash,password_salt,role,name,birth_date,ssn,department,graduation_date) SELECT 'race{index}',password_hash,password_salt,'STUDENT','Concurrency','2005-01-01','RACE-{index}','CS','2027-06-30' FROM users WHERE id=101; INSERT INTO enrollments(student_id,offering_id,grade) SELECT id,101,'A' FROM users WHERE username='race{index}'; INSERT INTO enrollments(student_id,offering_id,grade) SELECT id,102,'B' FROM users WHERE username='race{index}';")
clients = [Client('race'+str(i)) for i in range(12)]
with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
    codes = list(pool.map(lambda client: client.call('/schedule','PUT',valid)[0], clients))
check('concurrent capacity: five accepted and seven rejected', codes.count(200) == 5 and codes.count(409) == 7)
check('MySQL enrollment never exceeds capacity ten', sql('SELECT MAX(n) FROM (SELECT COUNT(*) n FROM enrollments WHERE offering_id>=200 GROUP BY offering_id) x;') == '10')
check('late submit to newly full course rejected', s.call('/schedule','PUT',valid)[0] == 409)
sql("DELETE e FROM enrollments e JOIN users u ON e.student_id=u.id WHERE u.username LIKE 'race%'; DELETE FROM users WHERE username LIKE 'race%';")
check('register schedule with teacherless primary for close scenario', s.call('/schedule','PUT',{'primary':[201,202,203,207],'alternates':[204,205],'submit':True})[0] == 200)
check('saving submitted edits retains previous enrollment', s.call('/schedule','PUT',{'primary':[205], 'alternates':[], 'submit':False})[0] == 200 and sql('SELECT COUNT(*) FROM enrollments WHERE student_id=101 AND offering_id>=200;') == '4')
check('draft editing preserves submitted alternate priority', sql('SELECT submitted_alternates FROM schedules WHERE student_id=101 AND semester_id=2;') == '[204, 205]')
s4 = Client('student04')
check('unsubmitted draft retained for closure test', s4.call('/schedule','PUT',{**valid,'submit':False})[0] == 200)
# Hold the same DB advisory lock as an in-flight registration transaction.
locker = subprocess.Popen(COMPOSE + ['exec','-T','db','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot -N -B --unbuffered registration'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
locker.stdin.write("SELECT GET_LOCK('course-registration-write',0); SELECT SLEEP(3);\n"); locker.stdin.close()
assert locker.stdout.readline().strip() == '1'
check('close rejected while registration transaction in progress', a.call('/admin/close-registration','POST',{})[0] == 409)
locker.wait(timeout=10)
check('finance outage simulation enabled', a.call('/admin/billing/availability','POST',{'available':False})[0] == 200)
code, closure = a.call('/admin/close-registration','POST',{})
check('close registration commits six submitted students', code == 200 and closure['bills'] == 6)
final = s.call('/schedule')[1]
check('teacherless primary replaced in alternate priority order', final['primary'] == [201,202,203,204] and final['status'] == 'FINAL')
check('five classes open and under-enrolled classes cancel', sql("SELECT GROUP_CONCAT(id ORDER BY id) FROM offerings WHERE semester_id=2 AND status='closed';") == '201,202,203,204,205' and sql("SELECT COUNT(*) FROM offerings WHERE semester_id=2 AND status='cancelled';") == '26')
check('unsubmitted draft excluded from billing', sql('SELECT COUNT(*) FROM billing_outbox WHERE student_id=104;') == '0')
check('tuition equals final course fees', sql('SELECT amount_cents FROM billing_outbox WHERE student_id=101;') == '360000')
for _ in range(30):
    if int(sql('SELECT COALESCE(MAX(attempts),0) FROM billing_outbox;')) > 0: break
    time.sleep(.2)
check('failed billing retained for retry', int(sql("SELECT COUNT(*) FROM billing_outbox WHERE status='PENDING' AND attempts>0;")) > 0)
a.call('/admin/billing/availability','POST',{'available':True}); a.call('/admin/billing/retry','POST',{})
for _ in range(40):
    if sql("SELECT COUNT(*) FROM billing_outbox WHERE status='DELIVERED';") == '6': break
    time.sleep(.2)
check('automatic billing delivery recovers after outage', sql("SELECT COUNT(*) FROM billing_outbox WHERE status='DELIVERED';") == '6')
check('finance receiver receives one record per student semester', sql('SELECT COUNT(*) FROM billing_receipts;') == '6')
# Simulate the sender losing a successful response: replay delivery must remain idempotent.
sql("UPDATE billing_outbox SET status='PENDING',next_attempt_at=NOW() WHERE student_id=101;")
for _ in range(30):
    if sql("SELECT status FROM billing_outbox WHERE student_id=101;") == 'DELIVERED': break
    time.sleep(.2)
check('duplicate delivery does not duplicate billing receipt', sql('SELECT COUNT(*) FROM billing_receipts;') == '6')
check('closed registration rejects student changes', s.call('/schedule','PUT',valid)[0] == 409)
check('closed registration rejects teaching changes', p.call('/teaching','PUT',{'offerings':[]})[0] == 409)
check('repeated closure rejected without duplicate bills', a.call('/admin/close-registration','POST',{})[0] == 409 and sql('SELECT COUNT(*) FROM billing_outbox;') == '6')
check('logout invalidates session', s.call('/logout','POST',{})[0] == 200 and s.call('/me')[0] == 401)
print(json.dumps({'passed':len(results),'seconds':round(time.perf_counter()-start,2),'tests':results},ensure_ascii=False,indent=2))
