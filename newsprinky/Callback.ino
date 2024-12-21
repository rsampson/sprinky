//Watering schedule settings callback========================================
void SaveScheduleCallback(Control *sender, int type) {
  if (type == B_UP) {
    stored_hour =   ESPUI.getControl(hourNumber)->value;  // RAS
    stored_minute = ESPUI.getControl(minuteNumber)->value;
        
    preferences.putString("hour", stored_hour);
    preferences.putString("minute", stored_minute);
    
//    runtime[0] = (ESPUI.getControl(slideID1)->value).toInt(); 
//    runtime[1] = (ESPUI.getControl(slideID2)->value).toInt(); 
//    runtime[2] = (ESPUI.getControl(slideID3)->value).toInt(); 
//    runtime[3] = (ESPUI.getControl(slideID4)->value).toInt(); 
//    runtime[4] = (ESPUI.getControl(slideID5)->value).toInt(); 
//    runtime[5] = (ESPUI.getControl(slideID6)->value).toInt();
//
    preferences.putString("slide1", ESPUI.getControl(slideID1)->value);
    preferences.putString("slide2", ESPUI.getControl(slideID2)->value);
    preferences.putString("slide3", ESPUI.getControl(slideID3)->value);
    preferences.putString("slide4", ESPUI.getControl(slideID4)->value);
    preferences.putString("slide5", ESPUI.getControl(slideID5)->value);
    preferences.putString("slide6", ESPUI.getControl(slideID6)->value);
    }
}
//Watering schedule settings settings callback===============================
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

void valveButtonCallback(Control *sender, int type) {
  if      (sender->value == String( "valve 1")) relayOn(0);   // only use 6 stations for now
  else if (sender->value == String( "valve 2")) relayOn(1);
  else if (sender->value == String( "valve 3")) relayOn(2);
  else if (sender->value == String( "valve 4")) relayOn(3);
  else if (sender->value == String( "valve 5")) relayOn(4);
  else if (sender->value == String( "valve 6")) relayOn(5);
  else { Serial.print("unknown sender");
  }
  generalCallback(sender, type);
  timer.in(60000, shutOff);  // turn off any manually activated valve after a minute
  manualOp = true;           // indicate in manual mode
}

void slideCallback(Control *sender, int type) {
  int slideVal = (sender->value).toInt();
  if      (sender->id == slideID1) runtime[0] = slideVal;  // only use 6 stations for now
  else if (sender->id == slideID2) runtime[1] = slideVal;
  else if (sender->id == slideID3) runtime[2] = slideVal;
  else if (sender->id == slideID4) runtime[3] = slideVal;
  else if (sender->id == slideID5) runtime[4] = slideVal;
  else if (sender->id == slideID6) runtime[5] = slideVal;
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

//ESP Reset=================================
void ESPReset(Control *sender, int type) {
  if (type == B_UP) {
    ESP.restart();
  }
}
//ESP Reset=================================
