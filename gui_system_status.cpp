#include "gui_system_status.h"
#include "sprinky.h"
#include <ESPUI.h>

void generalCallback(Control *sender, int type) {
  Serial.print("CB: id(");
  Serial.print(sender->id);
  Serial.print(") Type(");
  Serial.print(type);
  Serial.print(") '");
  Serial.print(sender->label);
  Serial.print("' = ");
  Serial.println(sender->value);
}

void switchCallback(Control* sender, int type) {
  switch (type) {
    case S_ACTIVE:
        state.disable = true;
        preferences.putBool("disable", true);
        ESPUI.updateControlLabel(ui.mainSwitcher, "Watering off");
        break;

    case S_INACTIVE:
        state.disable = false;
        ESPUI.updateControlLabel(ui.mainSwitcher, "Watering on");       
        preferences.putBool("disable", false);
        break;
  }
  generalCallback(sender, type);
}

void setUpSystemStatusTab() {
  auto maintab = ESPUI.addControl(Tab, "", "System Status");

  ui.timeLabel = ESPUI.addControl(Label, "Current Time", "", Wetasphalt, maintab, generalCallback);

  char styleBuff[30]; // temp buffer for css styles
  sprintf(styleBuff, "font-size: 25px;");
  ESPUI.setElementStyle(ui.timeLabel, styleBuff);

  ui.tempLabel = ESPUI.addControl(Label, "Outside Temperature", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(ui.tempLabel, styleBuff);
  ui.aveTempLabel = ESPUI.addControl(Label, "24 Hour Average Temperature", "", Wetasphalt, ui.tempLabel, generalCallback);

  ui.signalLabel = ESPUI.addControl(Label, "WiFi Signal Strength", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(ui.signalLabel, styleBuff);
  
  ui.runtimeLabel = ESPUI.addControl(Label, "Daily Total Run Time", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(ui.runtimeLabel, styleBuff);

  ui.mainSwitcher = ESPUI.addControl(Switcher, "Watering on/off", "0", Wetasphalt, maintab, switchCallback);
  
  ui.debugLabel = ESPUI.addControl(Label, "Status/Debug", "some message", Wetasphalt, maintab, generalCallback);

  ui.mainTime = ESPUI.addControl(Time, "", "", None, 0,
     [](Control *sender, int type) {
       if(type == TM_VALUE) { 
         ESPUI.updateLabel(ui.timeLabel, sender->value);      
       }
     });
}
