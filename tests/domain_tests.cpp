#include "domain.hpp"
#include <iostream>
#include <stdexcept>
using domain::json;
void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
json course(int id,int day,int enrolled=0){return json{{"id",id},{"course_id",id},{"professor_id",1},{"day",day},{"start_slot",1},{"end_slot",2},{"enrolled",enrolled},{"capacity",10},{"status","open"},{"prerequisites",json::array()}};}
int main(){
 try{
  auto a=course(1,1),b=course(2,1);check(domain::conflicts(a,b),"same slots overlap");b["start_slot"]=3;b["end_slot"]=4;check(!domain::conflicts(a,b),"adjacent slots allowed");
  a["enrolled"]=10;check(!domain::can_add(a,json::array(),{}).empty(),"capacity of ten enforced");a["enrolled"]=0;a["prerequisites"]=json::array({7});check(!domain::can_add(a,json::array(),{}).empty(),"missing prerequisite rejected");check(domain::can_add(a,json::array(),{7}).empty(),"passed prerequisite accepted");check(!domain::passed("I")&&!domain::passed("F"),"incomplete and fail do not pass");
  a=course(1,1,3);b=course(2,2,2);auto c=course(3,3,3);c["professor_id"]=nullptr;
  json offerings=json::array({a,b,c});json schedules=json::array({json{{"student_id",1},{"primary",json::array({1,3})},{"alternates",json::array({2})}}});
  auto events=domain::level(offerings,schedules,{});check(schedules[0]["primary"]==json::array({1,2}),"teacherless primary replaced by ordered alternate");check(offerings[1]["status"]=="closed"&&offerings[1]["enrolled"]==3,"alternate reaches minimum enrollment");check(offerings[2]["status"]=="cancelled","teacherless offering cancelled");check(events.size()==2,"close audit events recorded");
  offerings=json::array({course(4,1,2)});schedules=json::array({json{{"student_id",1},{"primary",json::array({4})},{"alternates",json::array()}}});domain::level(offerings,schedules,{});check(schedules[0]["primary"].empty(),"under enrolled final course removed");
  offerings=json::array({course(1,1,3),course(2,1,9),course(3,2,10),course(4,3,2)});schedules=json::array({json{{"student_id",1},{"primary",json::array({1})},{"alternates",json::array({2,3,4})}}});domain::level(offerings,schedules,{});check(schedules[0]["primary"]==json::array({1,4}),"level skips conflicting and full alternatives");
  std::cout<<"All domain tests passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
