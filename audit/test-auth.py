"""Compile the complete auth implementation against real ArduinoJson and host boundaries.

LittleFS simulates short writes, size mismatches, open/rename failure and rebooted
files. Linker malloc/realloc wrappers force genuine ArduinoJson allocation failure.
MD5Builder is a deterministic 32-hex test double; cryptographic strength and real
flash power-loss behavior are outside these persistence/rollback tests.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
json_include = root / ".pio/libdeps/nodemcuv2/ArduinoJson/src"
assert json_include.exists(), "Run PlatformIO to install ArduinoJson first"
with tempfile.TemporaryDirectory() as folder:
    tmp = Path(folder)
    (tmp / "Arduino.h").write_text(r'''
#pragma once
#define F(x) x
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
struct String : std::string {
 using std::string::string;
 String(const std::string &value):std::string(value){}
 bool equalsConstantTime(const String &other) const {return *this==other;}
};
struct {uint32_t getChipId(){return 1;} uint32_t getCycleCount(){return 2;}} ESP;
uint32_t micros(){return 3;}
void randomSeed(uint32_t){}
long random(long low,long){return low;}
''')
    (tmp / "MD5Builder.h").write_text(r'''
#pragma once
#include <Arduino.h>
struct MD5Builder {
 String input;
 void begin(){} void add(const String &value){input=value;} void calculate(){}
 String toString(){uint32_t hash=2166136261U;for(unsigned char c:input){hash=(hash^c)*16777619U;}
  char text[33];snprintf(text,sizeof text,"%08x%08x%08x%08x",hash,hash^1U,hash^2U,hash^3U);return text;}
};
''')
    (tmp / "LittleFS.h").write_text(r'''
#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
#include <algorithm>
#include <limits>
struct File {
 std::shared_ptr<std::string> contents;
 size_t offset=0,limit=std::numeric_limits<size_t>::max();
 bool sizeMismatch=false;
 explicit operator bool() const {return bool(contents);}
 size_t write(uint8_t byte){return write(&byte,1);}
 size_t write(const uint8_t *bytes,size_t length){size_t count=std::min(length,limit-contents->size());contents->append(reinterpret_cast<const char *>(bytes),count);return count;}
 int read(){return offset<contents->size() ? static_cast<unsigned char>((*contents)[offset++]) : -1;}
 size_t readBytes(char *buffer,size_t length){size_t count=std::min(length,contents->size()-offset);memcpy(buffer,contents->data()+offset,count);offset+=count;return count;}
 size_t size(){return contents->size()+(sizeMismatch ? 1 : 0);}
 void flush(){} void close(){}
};
struct MockFS {
 std::map<std::string,std::shared_ptr<std::string>> files;
 size_t writeLimit=std::numeric_limits<size_t>::max();
 bool failOpen=false,failRename=false,sizeMismatch=false;
 File open(const char *path,const char *mode){
  if(failOpen)return {};
  if(mode[0]=='w'){auto contents=std::make_shared<std::string>();files[path]=contents;return {contents,0,writeLimit,sizeMismatch};}
  auto found=files.find(path);return found==files.end() ? File{} : File{found->second};
 }
 bool exists(const char *path){return files.count(path);}
 bool remove(const char *path){return files.erase(path)!=0;}
 bool rename(const char *from,const char *to){if(failRename || !exists(from))return false;files[to]=files[from];files.erase(from);return true;}
 void put(const char *path,const std::string &value){files[path]=std::make_shared<std::string>(value);}
 std::string get(const char *path){return *files.at(path);}
 void reset(){files.clear();writeLimit=std::numeric_limits<size_t>::max();failOpen=failRename=sizeMismatch=false;}
} LittleFS;
''')
    code = r'''
#include <cassert>
#include <cstdlib>
#include <iostream>
bool failJsonAllocation=false;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *,size_t);
extern "C" void *__wrap_malloc(size_t size){return failJsonAllocation ? nullptr : __real_malloc(size);}
extern "C" void *__wrap_realloc(void *pointer,size_t size){return failJsonAllocation ? nullptr : __real_realloc(pointer,size);}
'''
    code += '#include "' + str(root / 'src/auth.cpp') + '"\n'
    code += r'''
AuthConfig candidate(const char *password){AuthConfig config{};config.version=AUTH_CONFIG_VERSION;
 copyString(config.passwordMd5,sizeof config.passwordMd5,computePasswordMd5(password).c_str());
 copyString(config.provisionedPassword,sizeof config.provisionedPassword,password);return config;}
std::string fixture(const AuthConfig &config){LittleFS.reset();assert(writeAuthConfig(config));return LittleFS.get(AUTH_CONFIG_PATH);}
void seed(const std::string &value){LittleFS.reset();LittleFS.put(AUTH_CONFIG_PATH,value);}
int main(){
 const AuthConfig oldConfig=candidate("old-password"),newConfig=candidate("new-password");
 const std::string oldJson=fixture(oldConfig),newJson=fixture(newConfig);
 seed(oldJson);assert(writeAuthConfig(newConfig));assert(LittleFS.get(AUTH_CONFIG_PATH)==newJson);assert(!LittleFS.exists("/auth.json.tmp"));
 for(size_t limit : {size_t(0),size_t(12),newJson.size()-1}) {
  seed(oldJson);LittleFS.writeLimit=limit;assert(!writeAuthConfig(newConfig));assert(LittleFS.get(AUTH_CONFIG_PATH)==oldJson);assert(!LittleFS.exists("/auth.json.tmp"));
 }
 seed(oldJson);LittleFS.sizeMismatch=true;assert(!writeAuthConfig(newConfig));assert(LittleFS.get(AUTH_CONFIG_PATH)==oldJson);assert(!LittleFS.exists("/auth.json.tmp"));
 seed(oldJson);failJsonAllocation=true;bool saved=writeAuthConfig(newConfig);failJsonAllocation=false;
 assert(!saved);assert(LittleFS.get(AUTH_CONFIG_PATH)==oldJson);assert(!LittleFS.exists("/auth.json.tmp"));
 seed(oldJson);LittleFS.failOpen=true;assert(!writeAuthConfig(newConfig));assert(LittleFS.get(AUTH_CONFIG_PATH)==oldJson);
 seed(oldJson);LittleFS.failRename=true;assert(!writeAuthConfig(newConfig));assert(LittleFS.get(AUTH_CONFIG_PATH)==oldJson);assert(LittleFS.get("/auth.json.tmp")==newJson);
 // After interrupted replacement, a valid canonical file remains authoritative.
 authInit();assert(authVerifyPassword("old-password"));assert(!authVerifyPassword("new-password"));
 // A lost/corrupt canonical file recovers a validated temporary configuration.
 for(bool corruptCanonical : {false,true}) {
  LittleFS.reset();if(corruptCanonical)LittleFS.put(AUTH_CONFIG_PATH,"{");LittleFS.put("/auth.json.tmp",newJson);
  authInit();assert(authVerifyPassword("new-password"));assert(LittleFS.get(AUTH_CONFIG_PATH)==newJson);assert(!LittleFS.exists("/auth.json.tmp"));assert(!authWasProvisionedThisBoot());
 }
 LittleFS.reset();LittleFS.put("/auth.json.tmp",newJson);LittleFS.failRename=true;
 authInit();assert(authVerifyPassword("new-password"));assert(LittleFS.get("/auth.json.tmp")==newJson);assert(!LittleFS.exists(AUTH_CONFIG_PATH));assert(!authWasProvisionedThisBoot());
 // Malformed JSON, unsupported versions and invalid hashes must never publish.
 for(const char *invalid : {"{", "{\"version\":99,\"passwordMd5\":\"00000000000000000000000000000000\"}","{\"version\":2,\"passwordMd5\":\"invalid\"}"}) {
  LittleFS.reset();LittleFS.put("/auth.json.tmp",invalid);authConfig=oldConfig;assert(!loadAuthConfig());assert(authVerifyPassword("old-password"));
 }
 // All save failure modes preserve both active credential and revealable state.
 for(int failure=0;failure<4;++failure) {
  seed(oldJson);authInit();provisionedThisBoot=true;
  if(failure==0)LittleFS.writeLimit=12;
  if(failure==1)LittleFS.failRename=true;
  if(failure==2)LittleFS.failOpen=true;
  if(failure==3)failJsonAllocation=true;
  String error;bool updated=authUpdatePassword("old-password","new-password",&error);failJsonAllocation=false;
  assert(!updated);assert(error=="Failed to save password");assert(authVerifyPassword("old-password"));assert(!authVerifyPassword("new-password"));
  assert(authCanRevealPassword());assert(std::string(authProvisionedPassword())=="old-password");assert(authWasProvisionedThisBoot());assert(LittleFS.get(AUTH_CONFIG_PATH)==oldJson);
 }
 seed(oldJson);authInit();String error;
 assert(authUpdatePassword("old-password","new-password",&error));assert(authVerifyPassword("new-password"));assert(!authVerifyPassword("old-password"));assert(!authCanRevealPassword());assert(!authWasProvisionedThisBoot());
 authInit();assert(authVerifyPassword("new-password"));assert(!authCanRevealPassword());
 std::cout<<"Auth regressions passed: complete/short/overflow writes, replacement/recovery, failed password rollback\n";
}
'''
    (tmp / "check.cpp").write_text(code)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I" + str(tmp), "-I" + str(json_include), str(tmp / "check.cpp"), "-Wl,--wrap=malloc,--wrap=realloc", "-o", str(tmp / "check")], check=True)
    subprocess.run([str(tmp / "check")], check=True)
