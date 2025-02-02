//Most elements in this test UI are assigned this generic callback which prints some
//basic information. Event types are defined in ESPUI.h
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

// The extended param can be used to pass additional information
void paramCallback(Control* sender, int type, int param)
{
  generalCallback(sender, type);
  Serial.print("param = ");
  Serial.println(param);
}

void textCallback(Control *sender, int type) {
  //This callback is needed to handle the changed values, even though it doesn't do anything itself.
}


extern uint32_t start_time_ms;
extern bool runCycle;
//Watering schedule settings callback========================================
void RunCallback(Control *sender, int type) {
  if (type == B_UP) {
    start_time_ms = millis();
    runCycle = true;   
  }
}

//Watering schedule settings callback========================================
void SaveScheduleCallback(Control *sender, int type) {
  if (type == B_UP) {
    // store run hour and minute
    stored_hour =   ESPUI.getControl(hourNumber)->value;  // RAS
    stored_minute = ESPUI.getControl(minuteNumber)->value;
        
    preferences.putString("hour", stored_hour);
    preferences.putString("minute", stored_minute);
    
    // store slider positions
    preferences.putString("slide1", ESPUI.getControl(slideID1)->value);
    preferences.putString("slide2", ESPUI.getControl(slideID2)->value);
    preferences.putString("slide3", ESPUI.getControl(slideID3)->value);
    preferences.putString("slide4", ESPUI.getControl(slideID4)->value);
#ifdef RELAY8
    preferences.putString("slide5", ESPUI.getControl(slideID5)->value);
    preferences.putString("slide6", ESPUI.getControl(slideID6)->value);
    preferences.putString("slide7", ESPUI.getControl(slideID7)->value);
    preferences.putString("slide8", ESPUI.getControl(slideID8)->value); 
#endif   
 
    // store valve name changes
    preferences.putString("name1", ESPUI.getControl(valve1Label)->value);
    preferences.putString("name2", ESPUI.getControl(valve2Label)->value);
    preferences.putString("name3", ESPUI.getControl(valve3Label)->value);
    preferences.putString("name4", ESPUI.getControl(valve4Label)->value);
#ifdef RELAY8
    preferences.putString("name5", ESPUI.getControl(valve5Label)->value);
    preferences.putString("name6", ESPUI.getControl(valve6Label)->value);
    preferences.putString("name7", ESPUI.getControl(valve7Label)->value);
    preferences.putString("name8", ESPUI.getControl(valve8Label)->value); 
#endif  

//    // rename buttons  
//    ESPUI.updateButton(button1Label, ESPUI.getControl(valve1Label)->value);
//    ESPUI.updateButton(button2Label, ESPUI.getControl(valve2Label)->value);
//    ESPUI.updateButton(button3Label, ESPUI.getControl(valve3Label)->value);
//    ESPUI.updateButton(button4Label, ESPUI.getControl(valve4Label)->value);
//#ifdef RELAY8
//    ESPUI.updateButton(button5Label, ESPUI.getControl(valve5Label)->value);
//    ESPUI.updateButton(button6Label, ESPUI.getControl(valve6Label)->value);
//    ESPUI.updateButton(button7Label, ESPUI.getControl(valve7Label)->value);
//    ESPUI.updateButton(button8Label, ESPUI.getControl(valve8Label)->value); 
//#endif    

    // rename sliders  
    ESPUI.updateLabel(slide1Label, ESPUI.getControl(valve1Label)->value);
    ESPUI.updateLabel(slide2Label, ESPUI.getControl(valve2Label)->value);
    ESPUI.updateLabel(slide3Label, ESPUI.getControl(valve3Label)->value);
    ESPUI.updateLabel(slide4Label, ESPUI.getControl(valve4Label)->value);
#ifdef RELAY8
    ESPUI.updateLabel(slide5Label, ESPUI.getControl(valve5Label)->value);
    ESPUI.updateLabel(slide6Label, ESPUI.getControl(valve6Label)->value);
    ESPUI.updateLabel(slide7Label, ESPUI.getControl(valve7Label)->value);
    ESPUI.updateLabel(slide8Label, ESPUI.getControl(valve8Label)->value); 
#endif    

  }
}
//END: Watering schedule settings settings callback===============================


// Valve control for test
void valveButtonCallback(Control *sender, int type) {
  if (type == B_UP) {
    allOff();
    timer.cancel();
    runCycle = false;
    if      (sender->value == String( "valve 1")) relayOn(0);   
    else if (sender->value == String( "valve 2")) relayOn(1);
    else if (sender->value == String( "valve 3")) relayOn(2);
    else if (sender->value == String( "valve 4")) relayOn(3);
#ifdef RELAY8 
    else if (sender->value == String( "valve 5")) relayOn(4);
    else if (sender->value == String( "valve 6")) relayOn(5);
    else if (sender->value == String( "valve 7")) relayOn(6);
    else if (sender->value == String( "valve 8")) relayOn(7);
#endif
    else  Serial.print("unknown sender");
    
    timer.in(60000, shutOff);  // turn off any manually activated valve after a minute
    generalCallback(sender, type);
  }
}

void slideCallback(Control *sender, int type) {
  int slideVal = (sender->value).toInt();
  if      (sender->id == slideID1) runtime[0] = slideVal;  
  else if (sender->id == slideID2) runtime[1] = slideVal;
  else if (sender->id == slideID3) runtime[2] = slideVal;
  else if (sender->id == slideID4) runtime[3] = slideVal;
#ifdef RELAY8 
  else if (sender->id == slideID5) runtime[4] = slideVal;
  else if (sender->id == slideID6) runtime[5] = slideVal;
  else if (sender->id == slideID7) runtime[6] = slideVal;
  else if (sender->id == slideID8) runtime[7] = slideVal;
#endif
  else { Serial.print("unknown sender");
  }
  generalCallback(sender, type);
}

void hourCallback(Control *sender, int type) {
  if(type == N_VALUE) { 
      webPrint("Run hour set: %s \n",(sender->value).c_str() );
      runHour =  (sender->value).toInt();
  }
   generalCallback(sender, type);
}

void minuteCallback(Control *sender, int type) {
   if(type == N_VALUE) { 
      webPrint("Run minute set: %s \n",(sender->value).c_str() );
      runMinute =  (sender->value).toInt();
  }
   generalCallback(sender, type);
}

void switchCallback(Control* sender, int type)
{
    switch (type)
    {
    case S_ACTIVE:
        disable = true;
        preferences.putBool("disable", true);
        break;

    case S_INACTIVE:
        disable = false;
        preferences.putBool("disable", false);
        break;
    }
   generalCallback(sender, type);
}

//WiFi settings callback=====================================================
void SaveWifiDetailsCallback(Control *sender, int type) {
  if (type == B_UP) {
    stored_ssid = ESPUI.getControl(wifi_ssid_text)->value;
    stored_pass = ESPUI.getControl(wifi_pass_text)->value;

    preferences.putString("ssid", stored_ssid);
    preferences.putString("pass", stored_pass);
  }
}
//WiFi settings callback=====================================================


//ESP Reset=================================
void ESPReset(Control *sender, int type) {
  if (type == B_UP) {
    ESP.restart();
  }
}
//ESP Reset=================================
