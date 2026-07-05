#include "sprinky.h"
#include <ESPUI.h>
#include "gui_system_status.h"
#include "gui_valve_controls.h"
#include "gui_setup_maintenance.h"

// ******************This is the main function which builds our GUI*******************

void setUpUI() {

#ifdef ESP8266
    { HeapSelectIram doAllocationsInIRAM;
#endif

  //Turn off verbose debugging
  ESPUI.setVerbosity(Verbosity::Quiet);

  //Make sliders continually report their position as they are being dragged.
  ESPUI.sliderContinuous = true;

  // Initialize the three tabs
  setUpSystemStatusTab();
  setUpValveControlsTab();
  setUpSetupMaintenanceTab();

  // Finally, start up the UI.
  ESPUI.begin(HOSTNAME);

#ifdef ESP8266
    } // HeapSelectIram
#endif

}