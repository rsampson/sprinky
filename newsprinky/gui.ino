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
  char styleBuff[30]; // temp buffer for css styles
  auto maintab = ESPUI.addControl(Tab, "", "System Status");
  
  timeLabel =    ESPUI.addControl(Label, "Current Time", "", Wetasphalt, maintab, generalCallback);
  // change lable font size
  sprintf(styleBuff, "font-size: 25px;");
  ESPUI.setElementStyle(timeLabel, styleBuff);
  bootLabel =    ESPUI.addControl(Label, "Boot Time", "", Wetasphalt, timeLabel, generalCallback);
  
  tempLabel =    ESPUI.addControl(Label, "Outside Temperature", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(tempLabel, styleBuff);
  aveTempLabel =    ESPUI.addControl(Label, "24 Hour Average Temperature", "", Wetasphalt, tempLabel, generalCallback);
    
  signalLabel =  ESPUI.addControl(Label, "WiFi Signal Strength", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(signalLabel, styleBuff);
  
  runtimeLabel =  ESPUI.addControl(Label, "Daily Total Run Time", "", Wetasphalt, maintab, generalCallback);
  ESPUI.setElementStyle(runtimeLabel, styleBuff);

  mainSwitcher = ESPUI.addControl(Switcher, "Watering Disable", "0", Wetasphalt, maintab, switchCallback);
  disable = preferences.getBool("disable", "0");
  ESPUI.updateSwitcher(mainSwitcher, disable);
  debugLabel =   ESPUI.addControl(Label, "Debug", "some message", Wetasphalt, maintab, generalCallback);
  
   mainTime = ESPUI.addControl(Time, "", "", None, 0,
     [](Control *sender, int type) {
       if(type == TM_VALUE) { 
        ESPUI.updateLabel(timeLabel, timeClient.getFormattedTime());
       }
   });

  /*
   * Tab: Valve controls
   *-----------------------------------------------------------------------------------------------------------*/
  auto grouptab = ESPUI.addControl(Tab, "", "Valve Controls");
  ESPUI.addControl(Separator, "Valve Diagnostics (open valve for two minutes)", "", None, grouptab);
  //The parent of this button is a tab, so it will create a new panel with one control.
  auto groupbutton = ESPUI.addControl(Button, "Valve Test", "valve 1", Wetasphalt, grouptab, valveButtonCallback);
  //However the parent of this button is another control, so therefore no new panel is
  //created and the button is added to the existing panel.
  ESPUI.addControl(Button, "", "valve 2", Wetasphalt, groupbutton, valveButtonCallback);
  ESPUI.addControl(Button, "", "valve 3", Wetasphalt, groupbutton, valveButtonCallback);
  ESPUI.addControl(Button, "", "valve 4", Wetasphalt, groupbutton, valveButtonCallback);
#ifdef RELAY8 
  ESPUI.addControl(Button, "", "valve 5", Wetasphalt, groupbutton, valveButtonCallback);
  ESPUI.addControl(Button, "", "valve 6", Wetasphalt, groupbutton, valveButtonCallback);
  ESPUI.addControl(Button, "", "valve 7", Wetasphalt, groupbutton, valveButtonCallback);
  ESPUI.addControl(Button, "", "valve 8", Wetasphalt, groupbutton, valveButtonCallback);
#endif 

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
        
  ESPUI.addControl(Separator, "Run Time Settings", "", None, grouptab);

  //Number inputs also accept Min and Max components, but you should still validate the values.
  hourNumber = ESPUI.addControl(Number, "Run Hour", "12", Wetasphalt, grouptab, hourCallback);
  ESPUI.addControl(Min, "", "0", None, hourNumber);
  ESPUI.addControl(Max, "", "23", None, hourNumber);
  //Number inputs also accept Min and Max components, but you should still validate the values.
  minuteNumber = ESPUI.addControl(Number, "Run Minute", "0", Wetasphalt, grouptab, minuteCallback);
  ESPUI.addControl(Min, "", "0", None, minuteNumber);
  ESPUI.addControl(Max, "", "60", None, minuteNumber); 

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
  ESPUI.setElementStyle(slide1Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, groupsliders);
  ESPUI.addControl(Max, "", "600", None, groupsliders);
  
  slideID2 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide2Label = ESPUI.addControl(Label, "", "valve 2", None, groupsliders);
  ESPUI.setElementStyle(slide2Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID2);
  ESPUI.addControl(Max, "", "600", None, slideID2);
  
  slideID3 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide3Label = ESPUI.addControl(Label, "", "valve 3", None, groupsliders);
  ESPUI.setElementStyle(slide3Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID3);
  ESPUI.addControl(Max, "", "600", None, slideID3);
  
  slideID4 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide4Label = ESPUI.addControl(Label, "", "valve 4", None, groupsliders);
  ESPUI.setElementStyle(slide4Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID4);
  ESPUI.addControl(Max, "", "600", None, slideID4);
#ifdef RELAY8  
  slideID5 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide5Label = ESPUI.addControl(Label, "", "valve 5", None, groupsliders);
  ESPUI.setElementStyle(slide5Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID5);
  ESPUI.addControl(Max, "", "600", None, slideID5);
  
  slideID6 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide6Label = ESPUI.addControl(Label, "", "valve 6", None, groupsliders);
  ESPUI.setElementStyle(slide6Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID6);
  ESPUI.addControl(Max, "", "600", None, slideID6);

  slideID7 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide7Label = ESPUI.addControl(Label, "", "valve 7", None, groupsliders);
  ESPUI.setElementStyle(slide7Label, clearLabelStyle);
  ESPUI.addControl(Min, "", "1", None, slideID7);
  ESPUI.addControl(Max, "", "600", None, slideID7);
  
  slideID8 = ESPUI.addControl(Slider, "", "600", None, groupsliders, slideCallback);
  slide8Label = ESPUI.addControl(Label, "", "valve 8", None, groupsliders);
  ESPUI.setElementStyle(slide8Label, clearLabelStyle);
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

  //Finally, start up the UI.
  //This should only be called once we are connected to WiFi.
  ESPUI.begin("Garden Watering System");

#ifdef ESP8266
    } // HeapSelectIram
#endif

}
