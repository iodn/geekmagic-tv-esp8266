"""Exercise settings persistence failure and RAM rollback without device flash."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/settings.cpp').read_text()
start = source.index('bool settingsSave(')
end = source.index('\n// Boot counter functions', start)
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#define F(x) x
struct Settings {int brightness;uint32_t crc;};
Settings normalizeSettingsForSave(const Settings &s){return s;}
uint32_t settingsCalculateCRC(const Settings &s){return uint32_t(s.brightness)^0x12345678;}
constexpr int SETTINGS_ADDR=0;
constexpr uint16_t SETTINGS_MAGIC=0xCAFE;
struct {
 std::array<uint8_t,512> ram{},flash{};bool succeeds=true;
 template<class T> void get(int pos,T &value){memcpy(&value,ram.data()+pos,sizeof(value));}
 template<class T> void put(int pos,const T &value){memcpy(ram.data()+pos,&value,sizeof(value));}
 bool commit(){if(!succeeds)return false;flash=ram;return true;}
} EEPROM;
struct {std::string message;void println(const char *s){message=s;}} Serial;
'''
code += source[start:end]
code += r'''
int main(){
 Settings old{30,0};assert(settingsSave(old));
 auto saved=EEPROM.flash;EEPROM.succeeds=false;
 Settings next{80,0};assert(!settingsSave(next));
 assert(EEPROM.ram==saved && EEPROM.flash==saved);
 assert(Serial.message=="Settings save failed");
 EEPROM.succeeds=true;uint8_t bootCounter=2;EEPROM.put(200,bootCounter);assert(EEPROM.commit());
 Settings persisted;memcpy(&persisted,EEPROM.flash.data()+2,sizeof persisted);
 assert(persisted.brightness==30);
 assert(settingsSave(next));memcpy(&persisted,EEPROM.flash.data()+2,sizeof persisted);
 assert(persisted.brightness==80 && persisted.crc==settingsCalculateCRC(next));
 assert(EEPROM.flash[200]==bootCounter);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path=Path(folder)
    (path/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('EEPROM settings regressions passed: failure reporting, RAM rollback, retry, counter preservation')
