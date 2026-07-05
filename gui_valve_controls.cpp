#include "gui_valve_controls.h"
#include "sprinky.h"
#include <ESPUI.h>

#ifdef RELAY8
constexpr int NUM_VALVES = 8;
#else
constexpr int NUM_VALVES = 4;
#endif

void RunCallback(Control *sender, int type) {
  if (type == B_UP) {
    allOff();
    timer.cancel(); // cancel any manual operations
    state.start_time_ms = millis();
    state.runCycle = true;
  }
}

void SaveScheduleCallback(Control *sender, int type) {
  if (type == B_UP) {
    // store run hour and minute
    stored_hour = ESPUI.getControl(ui.hourNumber)->value;
    stored_minute = ESPUI.getControl(ui.minuteNumber)->value;

    preferences.putString("hour", stored_hour);
    preferences.putString("minute", stored_minute);

    // store slider positions and valve names using clean loops
    for (int i = 0; i < NUM_VALVES; i++) {
      char slideKey[10];
      sprintf(slideKey, "slide%d", i + 1);
      preferences.putString(slideKey, ESPUI.getControl(ui.sliders[i])->value);

      char nameKey[10];
      sprintf(nameKey, "name%d", i + 1);
      preferences.putString(nameKey, ESPUI.getControl(ui.valves[i])->value);

      // rename sliders dynamically
      ESPUI.updateLabel(ui.slideLabels[i],
                        ESPUI.getControl(ui.valves[i])->value);
    }
  }
}

void valveButtonCallback(Control *sender, int type) {
  if (type == B_UP) {
    shutOff((void*)0);
    // timer.cancel(); allready in shutoff
    state.runCycle = false;

    // Use robust index-based search instead of string match
    int found_index = -1;
    for (int i = 0; i < NUM_VALVES; i++) {
      if (sender->id == ui.buttons[i]) {
        found_index = i;
        break;
      }
    }

    if (found_index != -1) {
      relayOn(found_index);
    } else {
      Serial.print("unknown sender");
    }

    timer.in(60000, shutOff); // turn off any manually activated valve after a minute
    generalCallback(sender, type);
  }
}

void slideCallback(Control *sender, int type) {
  int slideVal = (sender->value).toInt();

  // Use robust index-based search
  int found_index = -1;
  for (int i = 0; i < NUM_VALVES; i++) {
    if (sender->id == ui.sliders[i]) {
      found_index = i;
      break;
    }
  }

  if (found_index != -1) {
    state.runtime[found_index] = slideVal;
  } else {
    Serial.print("unknown sender");
  }

  generalCallback(sender, type);
}

void hourCallback(Control *sender, int type) {
  if (type == N_VALUE) {
    webPrint("Run hour set: %s \n", (sender->value).c_str());
    state.runHour = (sender->value).toInt();
  }
  generalCallback(sender, type);
}

void minuteCallback(Control *sender, int type) {
  if (type == N_VALUE) {
    webPrint("Run minute set: %s \n", (sender->value).c_str());
    state.runMinute = (sender->value).toInt();
  }
  generalCallback(sender, type);
}

void setUpValveControlsTab() {
  // recover the names of our valves from memory
  String valveNames[8];
  for (int i = 0; i < NUM_VALVES; i++) {
    char key[10];
    sprintf(key, "name%d", i + 1);
    char defVal[15];
    sprintf(defVal, "valve %d", i + 1);
    valveNames[i] = preferences.getString(key, defVal);
  }

  auto grouptab = ESPUI.addControl(Tab, "", "Valve Controls");
  ESPUI.addControl(Separator, "Valve Diagnostics (open valve for two minutes)",
                   "", None, grouptab);

  // Parent test button
  ui.buttons[0] = ESPUI.addControl(Button, "Valve Test", valveNames[0].c_str(),
                                   Wetasphalt, grouptab, valveButtonCallback);

  // Children test buttons
  for (int i = 1; i < NUM_VALVES; i++) {
    ui.buttons[i] =
        ESPUI.addControl(Button, "", valveNames[i].c_str(), Wetasphalt,
                         ui.buttons[0], valveButtonCallback);
  }

  // Valve names input fields
  ui.valves[0] =
      ESPUI.addControl(Text, "Set Valve Names", valveNames[0].c_str(),
                       Wetasphalt, grouptab, generalCallback);
  for (int i = 1; i < NUM_VALVES; i++) {
    ui.valves[i] = ESPUI.addControl(Text, "", valveNames[i].c_str(), Wetasphalt,
                                    ui.valves[0], generalCallback);
  }

  // Run start time
  ESPUI.addControl(Separator, "Run Time Settings", "", None, grouptab);

  ui.hourNumber = ESPUI.addControl(Number, "Run Hour", "12", Wetasphalt,
                                   grouptab, hourCallback);
  ESPUI.addControl(Min, "", "0", None, ui.hourNumber);
  ESPUI.addControl(Max, "", "23", None, ui.hourNumber);

  ui.minuteNumber = ESPUI.addControl(Number, "Run Minute", "0", Wetasphalt,
                                     grouptab, minuteCallback);
  ESPUI.addControl(Min, "", "0", None, ui.minuteNumber);
  ESPUI.addControl(Max, "", "60", None, ui.minuteNumber);

  stored_hour = preferences.getString("hour", "8");
  stored_minute = preferences.getString("minute", "0");

  state.runHour = stored_hour.toInt();
  state.runMinute = stored_minute.toInt();

  ESPUI.updateNumber(ui.hourNumber, state.runHour);
  ESPUI.updateNumber(ui.minuteNumber, state.runMinute);

  ESPUI.addControl(Button, "Run Watering Sequence", "Run", Wetasphalt, grouptab,
                   RunCallback);

  // Sliders panel
  ui.sliders[0] = ESPUI.addControl(Slider, "Run Time (in seconds)", "600",
                                   Wetasphalt, grouptab, slideCallback);
  ui.slideLabels[0] =
      ESPUI.addControl(Label, "", valveNames[0].c_str(), None, ui.sliders[0]);
  ESPUI.addControl(Min, "", "1", None, ui.sliders[0]);
  ESPUI.addControl(Max, "", "600", None, ui.sliders[0]);

  for (int i = 1; i < NUM_VALVES; i++) {
    ui.sliders[i] =
        ESPUI.addControl(Slider, "", "600", None, ui.sliders[0], slideCallback);
    ui.slideLabels[i] =
        ESPUI.addControl(Label, "", valveNames[i].c_str(), None, ui.sliders[0]);
    ESPUI.addControl(Min, "", "1", None, ui.sliders[i]);
    ESPUI.addControl(Max, "", "600", None, ui.sliders[i]);
  }

  // Retrieve settings and initialize sliders
  for (int i = 0; i < NUM_VALVES; i++) {
    char slideKey[10];
    sprintf(slideKey, "slide%d", i + 1);
    state.runtime[i] = preferences.getString(slideKey, "300").toInt();

    ESPUI.updateSlider(ui.sliders[i], state.runtime[i]);
    ESPUI.updateLabel(ui.slideLabels[i], valveNames[i]);
  }

  ESPUI.addControl(Button, "Save Schedule/ Valve Names", "Save", Wetasphalt,
                   grouptab, SaveScheduleCallback);
}
