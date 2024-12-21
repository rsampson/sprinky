# sprinky
Landscape irrigation sprinkler controller.

"newsprinky" is a replacement for the earlier "SuperSprink" and "oldsprinky" controllers which are now deprecated. 

The initial idea for SuperSprink was to make the remote valve controllers as dumb as a rock and to do all of the control logic in python on a RaspberryPi server. This turned out to be a bad idea as the Pi would occasionally go down and possibly leave a valve open or leave my wife's plants unwatered. Not good.

Sprinky is an effort to fix some of my earlier failures. It runs stand alone on commercially available esp32 relay boards. It it uses the excellent ESPUI gui library written by Lukas Bachschwell so it has an easy to use interface. It also uses the equally excellent ElegantOTA library by Ayush Sharma for easy code updates without shleping your laptop out into the garden.

If it is not set up to attach to a local wifi AP, it will form it's own AP at 192.168.4.1. I've had a version of this running in my garden for about two years with no watering or plant disasters, however, use this code at your own risk. It is a work in progress and I am a lame coder.

Note: Your ElegantOTA library will need to be switched to asynch mode (see library doc https://github.com/ayushsharma82/ElegantOTA)
