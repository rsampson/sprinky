// ******************This is the main function which builds our GUI*******************

void setUpUI() {

#ifdef ESP8266
    { HeapSelectIram doAllocationsInIRAM;
#endif

  //Turn off verbose debugging
  ESPUI.setVerbosity(Verbosity::Quiet);

  //Make sliders continually report their position as they are being dragged.
  ESPUI.sliderContinuous = true;

  /*
   * Tab: Basic Controls
   * This tab contains all the basic ESPUI controls, and shows how to read and update them at runtime.
   *-----------------------------------------------------------------------------------------------------------*/
  auto maintab = ESPUI.addControl(Tab, "", "System Status");

  timeLabel =    ESPUI.addControl(Label, "Current Time", "", Wetasphalt, maintab, generalCallback);

  // change lable font size
  char styleBuff[30]; // temp buffer for css styles
  sprintf(styleBuff, "font-size: 25px;");
  ESPUI.setElementStyle(timeLabel, styleBuff);

  // bootLabel =    ESPUI.addControl(Label, "Boot Time", "", Wetasphalt, timeLabel, generalCallback);
  // bootTime = " boot up @ " + timeClient.getFormattedTime() + ", "  + Days[weekday()] ;  
  // ESPUI.updateLabel(bootLabel,  String(bootTime));

  tempLabel =    ESPUI.addControl(Label, "Outside Temperature", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(tempLabel, styleBuff);
  aveTempLabel =    ESPUI.addControl(Label, "24 Hour Average Temperature", "", Wetasphalt, tempLabel, generalCallback);


  signalLabel =  ESPUI.addControl(Label, "WiFi Signal Strength", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(signalLabel, styleBuff);
  
  runtimeLabel =  ESPUI.addControl(Label, "Daily Total Run Time", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(runtimeLabel, styleBuff);

  mainSwitcher = ESPUI.addControl(Switcher, "Watering Disable", "0", Wetasphalt, maintab, switchCallback);
  
  debugLabel =   ESPUI.addControl(Label, "Status/Debug", "some message", Wetasphalt, maintab, generalCallback);

  // This will recover time from the browser if not connected to the internet
  
   mainTime = ESPUI.addControl(Time, "", "", None, 0,
     [](Control *sender, int type) {
       if(type == TM_VALUE) { 
        //ESPUI.updateLabel(timeLabel, timeClient.getFormattedTime());
        ESPUI.updateLabel(timeLabel, sender->value);      }
   });


  /*
   * Tab: Valve controls
   *-----------------------------------------------------------------------------------------------------------*/
   // recover the names of our valves from memory
   String name1,name2,name3,name4,name5,name6,name7,name8;
   
  name1 =  preferences.getString("name1", "valve 1");
  name2 =  preferences.getString("name2", "valve 2");
  name3 =  preferences.getString("name3", "valve 3");
  name4 =  preferences.getString("name4", "valve 4");
#ifdef RELAY8
  name5 =  preferences.getString("name5", "valve 5");
  name6 =  preferences.getString("name6", "valve 6");
  name7 =  preferences.getString("name7", "valve 7");
  name8 =  preferences.getString("name8", "valve 8"); 
#endif

  auto grouptab = ESPUI.addControl(Tab, "", "Valve Controls");
  ESPUI.addControl(Separator, "Valve Diagnostics (open valve for two minutes)", "", None, grouptab);
  //The parent of this button is a tab, so it will create a new panel with one control.
  groupbutton = ESPUI.addControl(Button, "Valve Test", "valve 1", Wetasphalt, grouptab, valveButtonCallback);
  //However the parent of this button is another control, so therefore no new panel is
  //created and the button is added to the existing panel.
  button2Label = ESPUI.addControl(Button, "", "valve 2", Wetasphalt, groupbutton, valveButtonCallback);
  button3Label = ESPUI.addControl(Button, "", "valve 3", Wetasphalt, groupbutton, valveButtonCallback);
  button4Label = ESPUI.addControl(Button, "", "valve 4", Wetasphalt, groupbutton, valveButtonCallback);
#ifdef RELAY8 
  button5Label = ESPUI.addControl(Button, "", "valve 5", Wetasphalt, groupbutton, valveButtonCallback);
  button6Label = ESPUI.addControl(Button, "", "valve 6", Wetasphalt, groupbutton, valveButtonCallback);
  button7Label = ESPUI.addControl(Button, "", "valve 7", Wetasphalt, groupbutton, valveButtonCallback);
  button8Label = ESPUI.addControl(Button, "", "valve 8", Wetasphalt, groupbutton, valveButtonCallback);
#endif 
 //name buttons  ******************* this crashes ******************
//  ESPUI.updateControlValue(button1Label, name1 );
//  ESPUI.updateControlValue(button2Label, name2 );
//  ESPUI.updateControlValue(button3Label, name3 );
//  ESPUI.updateControlValue(button4Label, name4 );
// #ifdef RELAY8
//  ESPUI.updateControlValue(button5Label, name5 );
//  ESPUI.updateControlValue(button6Label, name6 );
//  ESPUI.updateControlValue(button7Label, name7 );
//  ESPUI.updateControlValue(button8Label, name8 ); 
// #endif  


// valve names ----------------------------------------------------------------------------

  valve1Label =  ESPUI.addControl(Text, "Set Valve Names", "valve 1", Wetasphalt, grouptab,    generalCallback);
  valve2Label =  ESPUI.addControl(Text, "", "valve 2", Wetasphalt, valve1Label, generalCallback);
  valve3Label =  ESPUI.addControl(Text, "", "valve 3", Wetasphalt, valve1Label, generalCallback);
  valve4Label =  ESPUI.addControl(Text, "", "valve 4", Wetasphalt, valve1Label, generalCallback);
#ifdef RELAY8   
  valve5Label =  ESPUI.addControl(Text, "", "valve 5", Wetasphalt, valve1Label, generalCallback);
  valve6Label =  ESPUI.addControl(Text, "", "valve 6", Wetasphalt, valve1Label, generalCallback);
  valve7Label =  ESPUI.addControl(Text, "", "valve 7", Wetasphalt, valve1Label, generalCallback);
  valve8Label =  ESPUI.addControl(Text, "", "valve 8", Wetasphalt, valve1Label, generalCallback);
#endif 

 // recall valve names from memory
  ESPUI.updateText(valve1Label,  preferences.getString("name1", "valve 1"));
  ESPUI.updateText(valve2Label,  preferences.getString("name2", "valve 2"));
  ESPUI.updateText(valve3Label,  preferences.getString("name3", "valve 3"));
  ESPUI.updateText(valve4Label,  preferences.getString("name4", "valve 4"));
#ifdef RELAY8
  ESPUI.updateText(valve5Label,  preferences.getString("name5", "valve 5"));
  ESPUI.updateText(valve6Label,  preferences.getString("name6", "valve 6"));
  ESPUI.updateText(valve7Label,  preferences.getString("name7", "valve 7"));
  ESPUI.updateText(valve8Label,  preferences.getString("name8", "valve 8")); 
#endif 
        
  // Run start time       
  ESPUI.addControl(Separator, "Run Time Settings", "", None, grouptab);
  //Number inputs also accept Min and Max components, but you should still validate the values.
  hourNumber = ESPUI.addControl(Number, "Run Hour", "12", Wetasphalt, grouptab, hourCallback);
  ESPUI.addControl(Min, "", "0", None, hourNumber);
  ESPUI.addControl(Max, "", "23", None, hourNumber);
  //Number inputs also accept Min and Max components, but you should still validate the values.
  minuteNumber = ESPUI.addControl(Number, "Run Minute", "0", Wetasphalt, grouptab, minuteCallback);
  ESPUI.addControl(Min, "", "0", None, minuteNumber);
  ESPUI.addControl(Max, "", "60", None, minuteNumber); 

  runHour = (stored_hour = preferences.getString("hour", "8")).toInt();
  runMinute= (stored_minute = preferences.getString("minute", "0")).toInt();

  ESPUI.updateNumber(hourNumber, stored_hour.toInt());
  ESPUI.updateNumber(minuteNumber, stored_minute.toInt());   


  ESPUI.addControl(Button, "Run Watering Sequence", "Run", Wetasphalt, grouptab, RunCallback);  
  
  ESPUI.addControl(Button, "Save Schedule/ Valve Names", "Save", Wetasphalt, grouptab, SaveScheduleCallback);
  
  
  //************Sliders************** can be grouped as well 

  //To label each slider in the group, we are going add additional labels and give them custom CSS styles
  //We need this CSS style rule, which will remove the label's background and ensure that it takes up the entire width of the panel
   String clearLabelStyle = "background-color: unset; width: 100%;";
  //First we add the main slider to create a panel
  
  groupsliders = ESPUI.addControl(Slider, "Run Time (in seconds)", "600", Wetasphalt, grouptab, slideCallback);
  //Then we add a label and set its style to the clearLabelStyle
  slide1Label = ESPUI.addControl(Label, "", "valve 1", None, groupsliders);
  //ESPUI.setElementStyle(slide1Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, groupsliders);
  ESPUI.addControl(Max, "", "600", None, groupsliders);
  
  slideID2 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide2Label = ESPUI.addControl(Label, "", "valve 2", None, groupsliders);
  //ESPUI.setElementStyle(slide2Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID2);
  ESPUI.addControl(Max, "", "600", None, slideID2);
  
  slideID3 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide3Label = ESPUI.addControl(Label, "", "valve 3", None, groupsliders);
  //ESPUI.setElementStyle(slide3Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID3);
  ESPUI.addControl(Max, "", "600", None, slideID3);
  
  slideID4 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide4Label = ESPUI.addControl(Label, "", "valve 4", None, groupsliders);
  //ESPUI.setElementStyle(slide4Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID4);
  ESPUI.addControl(Max, "", "600", None, slideID4);

#ifdef RELAY8  
  slideID5 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide5Label = ESPUI.addControl(Label, "", "valve 5", None, groupsliders);
  //ESPUI.setElementStyle(slide5Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID5);
  ESPUI.addControl(Max, "", "600", None, slideID5);
  
  slideID6 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide6Label = ESPUI.addControl(Label, "", "valve 6", None, groupsliders);
  //ESPUI.setElementStyle(slide6Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID6);
  ESPUI.addControl(Max, "", "600", None, slideID6);

  slideID7 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide7Label = ESPUI.addControl(Label, "", "valve 7", None, groupsliders);
  //ESPUI.setElementStyle(slide7Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID7);
  ESPUI.addControl(Max, "", "600", None, slideID7);
  
  slideID8 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide8Label = ESPUI.addControl(Label, "", "valve 8", None, groupsliders);
  //ESPUI.setElementStyle(slide8Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID8);
  ESPUI.addControl(Max, "", "600", None, slideID8);
#endif
  
  // retrieve settings and initialize sliders
  runtime[0] = preferences.getString("slide1", "300").toInt(); 
  runtime[1] = preferences.getString("slide2", "300").toInt(); 
  runtime[2] = preferences.getString("slide3", "300").toInt(); 
  runtime[3] = preferences.getString("slide4", "300").toInt(); 
#ifdef RELAY8   
  runtime[4] = preferences.getString("slide5", "300").toInt(); 
  runtime[5] = preferences.getString("slide6", "300").toInt(); 
  runtime[6] = preferences.getString("slide7", "300").toInt(); 
  runtime[7] = preferences.getString("slide8", "300").toInt(); 
#endif

  ESPUI.updateSlider(slideID1, runtime[0]); 
  ESPUI.updateSlider(slideID2, runtime[1]); 
  ESPUI.updateSlider(slideID3, runtime[2]); 
  ESPUI.updateSlider(slideID4, runtime[3]);
#ifdef RELAY8   
  ESPUI.updateSlider(slideID5, runtime[4]); 
  ESPUI.updateSlider(slideID6, runtime[5]); 
  ESPUI.updateSlider(slideID7, runtime[6]); 
  ESPUI.updateSlider(slideID8, runtime[7]); 
 #endif

 //name sliders  
  ESPUI.updateLabel(slide1Label, name1);
  ESPUI.updateLabel(slide2Label, name2);
  ESPUI.updateLabel(slide3Label, name3);
  ESPUI.updateLabel(slide4Label, name4);
#ifdef RELAY8
  ESPUI.updateLabel(slide5Label, name5);
  ESPUI.updateLabel(slide6Label, name6);
  ESPUI.updateLabel(slide7Label, name7);
  ESPUI.updateLabel(slide8Label, name8); 
#endif    
 
  /*
   * Tab: WiFi Credentials
   * You use this tab to enter the SSID and password of a wifi network to autoconnect to.
   *-----------------------------------------------------------------------------------------------------------*/
  auto wifitab = ESPUI.addControl(Tab, "", "WiFi Credentials");
  wifi_ssid_text = ESPUI.addControl(Text, "SSID", "", Wetasphalt, wifitab, textCallback);
  //Note that adding a "Max" control to a text control sets the max length
  ESPUI.addControl(Max, "", "32", None, wifi_ssid_text);
  wifi_pass_text = ESPUI.addControl(Text, "Password", "", Wetasphalt, wifitab, textCallback);
  ESPUI.addControl(Max, "", "64", None, wifi_pass_text);
  ESPUI.addControl(Button, "Save", "Save", Wetasphalt, wifitab, SaveWifiDetailsCallback);
    
  /*
   * Tab:System Maintenance
   * You use this tab to upload new code OTA, see ElegantOTA library doc
   *-----------------------------------------------------------------------------------------------------------*/
   auto maintenancetab = ESPUI.addControl(Tab, "", "System Maintenance");
   auto updateButton =   ESPUI.addControl(Label, "Code Update", "<a href=\"/update\"> <button>Update</button></a>", Wetasphalt, maintenancetab, generalCallback); 
   ESPUI.setElementStyle(updateButton , clearLabelStyle);
   ESPUI.addControl(Button, "", "Reboot",  Wetasphalt,  updateButton, ESPReset);

  // *********how to add an extended web page**********
//  ESPUI.WebServer()->on("/narf", HTTP_GET, [](AsyncWebServerRequest *request) {
//  request->send(200, "text/html", "<A HREF = \"http://192.168.0.74:8080/\">Rear Controller</A>");
//  });

  //Finally, start up the UI.
  //char title[] = " Garden Watering System";
  //This should only be called once we are connected to WiFi.
  ESPUI.begin(HOSTNAME);


#ifdef ESP8266
    } // HeapSelectIram
#endif

}