#include "gui_setup_maintenance.h"
#include "sprinky.h"
#include <ESPUI.h>

void textCallback(Control *sender, int type) {
  //This callback is needed to handle the changed values, even though it doesn't do anything itself.
}

void SaveWifiDetailsCallback(Control *sender, int type) {
  if (type == B_UP) {
    stored_ssid = ESPUI.getControl(ui.wifiSSIDText)->value;
    stored_pass = ESPUI.getControl(ui.wifiPassText)->value;

    preferences.putString("ssid", stored_ssid);
    preferences.putString("pass", stored_pass);
  }
}

void TZcallback(Control* sender, int type) {
  if      (sender->value == String( "AEST")) tz = &ausET;   
  else if (sender->value == String( "MSK")) tz = &tzMSK;
  else if (sender->value == String( "CE")) tz = &CE;
  else if (sender->value == String( "GMT")) tz = &UK;
  else if (sender->value == String( "UTC")) tz = &UTC;
  else if (sender->value == String( "EST")) tz = &usET;
  else if (sender->value == String( "CST")) tz = &usCT;
  else if (sender->value == String( "MST")) tz = &usMT;
  else if (sender->value == String( "AZT")) tz = &usAZ;
  else if (sender->value == String( "PST")) tz = &usPT;
  else Serial.println("Bad TZ selection");
  
  preferences.putString("timezone", (sender->value).c_str()); // set and store time zone selection
  printTZ(tz);
  setTime(getNtpTime());

  generalCallback(sender, type);
}

void ESPReset(Control *sender, int type) {
  if (type == B_UP) {
    ESP.restart();
  }
}

void setUpSetupMaintenanceTab() {
  auto maintenancetab = ESPUI.addControl(Tab, "", "Setup and Maintenance");

  ui.wifiSSIDText = ESPUI.addControl(Text, "SSID", "", Wetasphalt, maintenancetab, textCallback);
  ESPUI.addControl(Max, "", "32", None, ui.wifiSSIDText);
  
  ui.wifiPassText = ESPUI.addControl(Text, "Password", "", Wetasphalt, maintenancetab, textCallback);
  ESPUI.addControl(Max, "", "64", None, ui.wifiPassText);

#ifdef USE_WITH_HA
  ui.mqttUserText = ESPUI.addControl(Text, "MQTT user", "", Wetasphalt, maintenancetab, textCallback);
  ESPUI.addControl(Max, "", "32", None, ui.mqttUserText);
  
  ui.mqttPassText = ESPUI.addControl(Text, "MQTT Password", "", Wetasphalt, maintenancetab, textCallback);
  ESPUI.addControl(Max, "", "32", None, ui.mqttPassText);

  ui.mqttBrokerText = ESPUI.addControl(Text, "MQTT broker address", "", Wetasphalt, maintenancetab, textCallback);
  ESPUI.addControl(Max, "", "32", None, ui.mqttBrokerText);
#endif 

  ESPUI.addControl(Button, "Save", "Save", Wetasphalt, maintenancetab, SaveWifiDetailsCallback);
    
  auto updateButton = ESPUI.addControl(Label, "Code Update", "<a href=\"/update\"> <button>Update</button></a>", Wetasphalt, maintenancetab, generalCallback); 

  String clearLabelStyle = "background-color: unset; width: 100%;";
  ESPUI.setElementStyle(updateButton, clearLabelStyle);
  ESPUI.addControl(Button, "", "Reboot", Wetasphalt, updateButton, ESPReset);

  uint16_t select1 = ESPUI.addControl(ControlType::Select, "Select Time Zone", "", ControlColor::Wetasphalt, maintenancetab, &TZcallback);
  ESPUI.addControl(ControlType::Option, "Australia Eastern TZ", "AEST", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "Moscow", "MSK", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "Central European TZ", "CET", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "GMT", "GMT", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "Universal Time", "UTC", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "Eastern Standard Time", "EST", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "Central Standard Time", "CST", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "Mountain Standard Time", "MST", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "Arizona Time", "AZT", ControlColor::Wetasphalt, select1);
  ESPUI.addControl(ControlType::Option, "Pacific Standard Time", "PST", ControlColor::Wetasphalt, select1);
  
  ui.timeZoneLabel = ESPUI.addControl(Label, "Currently Selected Time Zone", "", ControlColor::Wetasphalt, maintenancetab);
}
