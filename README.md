# Bike Tracker v2.0
## Background
There are actually plenty of bicycle trackers around, so why make your own? Well, so that it works the way you want it… And it’s fun!

I was looking for a tracker that would let me know if the bike is moving and that will start tracking it through GPS if it’s stolen. I did not want it to work through bluetooth, because that requires proximity to the device and that enough users of the same tracker are around (never liked that concept). And I did not want to tie myself into using an app that likely will stop working if the company goes belly up. Most of the time, you’re also forced to use the SIM-card provided by the company that sells the device. Nah… I want to be in control of this thing.

So, I needed a microcontroller that I could hook an accelerometer and a GPS to, and that reliably could send me the data gathered from those sensors. A simple SMS seemed like the best way to transmit, and it’s unlikely to stop working in the near future.

## Future
Currently I'm satisfied with both hardware and software of the tracker. No changes planned for now...

## Hardware
After some research (and trial and error) I ended up with the following hardware:
* Arduino MKR GSM 1400 as the mainboard
* GSM/GPRS 3dBi mini antenna (5 cm)
* SMA to IPX UFL cable (60 cm)
* ADXL345 3-axis accelerometer
* Arduino MKR GPS Shield
* 2 3.7V 2200mAh Li-Po 18650 battery cells
* 2 single 18650 cell battery holders (taken from 4400mAh battery pack)
* Li-Po battery control circuit (taken from 4400mAh battery pack)
* On-off micro switch
* Sparkfun LiPo charger plus
* Short USB cable (15 cm, USB C male to micro USB female)
* JST PHR-2 connector
* Suitable cables
* Nuts, bolts, screws and various parts for mounting

A note on the charger listed above:
While testing the hardware I noticed that the MKR GSM board wouldn’t charge the Li-Po batteries I had, and after some further research I concluded that there was either something wrong with the board or that it simply wasn’t compatible with the batteries I had. Since it was functioning otherwise, and that I didn’t want to spend a lot of time investigating further I decided on using an external charging circuit instead and got the Sparkfun charger. If you make something similar and the MKR GSM board charges your battery, this external charger will of course not be necessary.

## Wiring
![Wiring diagram](/images/wiring.png)
![Closeup of main board](/images/board_closeup.jpg)
### MKR GMS 1400
I couldn’t find the MKR GSM 1400 without the headers (don't think it's even available without), so those had to be desoldered before anything else could be connected to the board. The headers just take up too much space for them to be kept on there.

Pull the plastic blocks off the pins with a pair of pliers and then desolder each pin individually. Unless it gets really messy there’s also no need to remove any of the old solder, and it might even be that the solder that is left on the board can be used for the wires later. It might be necessary to create a new hole for the wires though, but this can be achieved by pushing a toothpick through the solder while heating it up.

Pin 10 and the reset pin have been connected so that the board can be reset programmatically when needed.

To save a little bit of battery the power-LED (marked "ON" on the main board, as seen in the diagram above) has been desoldered.

Since the board needs to fit inside the mounting groove on the seat post (see _"Mounting on the bike"_ below) I had to remove the I2C and battery ports. They added to much bulk. Instead, the connectors to battery and GPS module where soldered directly to the board.

### MKR GPS Shield
Since the I2C port had to be removed (see above) the provided I2C cable could not be used to connect to the GPS shield. But, any other solution would become to bulky to fit. Therefore I chose to simply cut the I2C cable and solder it directly to the main board. Instead of using the regular pins I used the connectors for the old I2C port, which was rather difficult but I chose this route since I could not figure out how to use the GPS module with the Sparkfun u-blox GNSS library when the GPS was used in a shield configuration. The library would simply report that the module could not be found on the default I2C address and changing the address would not help. I also tried changing to using UART (as the Arduino GPS library does in the shield configuration), but this was also unsuccessful. If anyone reading this knows how to get the Sparkfun u-blox library to work when not using the I2C ports, please let me know...

To prevent the connector to loosen due to vibrations a small spot of glue was added to the edge after connecting the cable to the GPS module.

### ADXL345 Accelerometer
The following pins are connected between the main board and the accelerometer.  

MKR GSM 1400 | ADXL345
------------ | -------
Pin 6 | INT1
Pin 8 | INT2
Pin 11 | SDA
Pin 12 | SCL
GND | GND
VCC | VCC

### Sparkfun LiPo Charger Plus
The positive wire goes through the micro switch to the positive lead on the JST PHR-2 connector that connects to the main board. The negative wire goes directly to the negative lead on the JST PHR-2 connector.

## Software
The code for this project is found on Github:
https://www.github.com/johan-m-o/BikeTracker

I’m not actually a programmer (and this is my first ever Arduino project), I just like to dabble. As a result, the code might not be very pretty, effective or “correct”, but it works (from what I've seen at least). If you’ve got the time and skill, please feel free to contribute.

## Features
### Basics
The basic features include (a more detailed description follow):
* Up to 10 days battery time.
* Movement detection.
* GPS and GPRS location.
* Alerts through SMS.
* Control the device through SMS-commands.

### Details
#### Running time
With regular use the battery should last up to 10 days. This is if the device is on most of the time, but in deep sleep mode. With more activity, the battery will of course last a shorter time.

According to the [https://content.arduino.cc/assets/mkr-microchip_samd21_family_full_datasheet-ds40001882d.pdf](SAMD21 data sheet) there's no need to configure unused pins to optimise power consumption (floating pins). Therefore, I haven't bothered with that...

To charge the device I'm using a small USB extension cable that is accessible even with everything mounted on the bike. A power bank is a handy way of charging the tracker without having to bring the bike to somewhere where you have an outlet (if none is easily accessible where you keep the bike, that is).

#### Deep sleep
To use the least amount of battery, the device will go into deep sleep mode if no movement is registered and no location transmission is active. By default this happens after 5 minutes. In deep sleep mode only movement or the RTC can wake up the device, everything else is shut off or in low power mode. The device will wake up at set times (by default 8:00 and 20:00 o’clock), to transmit the battery level and possibly receive SMS commands (more on that below). If nothing happens within 5 minutes the device goes back to deep sleep.

If the device has been in a location sending mode, and once the device stops sending the location data, the period until it goes back into deep sleep mode is set to 20 minutes. This is so that you can have a longer window of opportunity to send SMS-commands (see below), for example manually triggering a location ping.

#### Idle mode
Idle mode is very similar to the deep sleep mode, but the device doesn't actually go to sleep. The only differenci is in which components are deactivated. The GPS module is shut off but the main board and modem are still active, so SMS messages can be recieved. The modem is not in low power mode though, since this causes a lot of instability on a running system (from what I've seen during tests). Battery life will of course be much shorter compared to when using deep sleep (battery should last up to 4 days).

Idle mode can be activated with an SMS command (see below).

#### Movement detection
If the tracker moves, you will be notified through an SMS. If things then stay quiet for a certain amount of time (default 30 seconds), another SMS letting you know that everything is quiet again is sent out.

#### Location
When things don’t quiet down, but movement is continuously registered over a period of 1.5 minutes the GPS will activate and start transmitting. If no GPS lock can be acquired within one minute the device will instead try to get the location from the mobile network, via GPRS (much less accurate, but can get an approximate location when the device is indoors or otherwise cannot get a GPS fix).
The location of the device will be transmitted every 2 minutes for a minimum of 5 times (not including the initial location ping). If there’s still movement after 5 successful location pings the device will continue to send the location 5 more times. But if no movement is registered once the 5th location message goes out the device will stop transmitting its location. If the continious GPS mode has been enabled the location will keep transmitting until the *GPS stop* command is recieved, even if the device is no longer moving.

#### SMS-commands
There are a number of commands that can be sent to the device. The prerequisite is that the device is on and that it isn’t in deep sleep mode (although once the device wakes up the command will most likely be recieved).

To send a command, create an SMS that starts with your device password (see the setup section below), directly followed by the command. The password can contain any characters or numbers you’d like.

**Example**: thisismypasswordgpsloc

In this example, "*thisismypassword*" is the used password, and the used command would trigger a single location ping.

**Restart**  
*Command: restart*  
Reset the device, as if you had toggled  the power button off and on.

**GPS Location**  
*Command: gpsloc*  
Trigger a single location ping.

**GPS Start**  
*Command: gps  
Alternative command: gps5*  
Start a location transmitting cycle. If the command is followed by a number (1 or greater), the time between sending GPS locations will be set to this amount of minutes.

**Continious GPS**  
*Command: gpscont*  
Do not stop sending the GPS location until the *GPS stop* command is recieved.

**GPS Timer**  
*Command: gpstimer5*  
Sets the time (in minutes) between sending GPS locations to the number that the command ends with (1 or greater). This command can be sent even when GPS is not yet active and the set timer will be used the next time GPS locations start sending.

**GPS Stop**  
*Command: gpsstop*  
Stop transmitting the device location.

**Deep sleep**  
*Command: deepsleep  
Alternative command: deepsleep12*  
Manually enter deep sleep mode. If the command is followed by a number between 1 and 24 (24-hour clock, 24 is midnight), the wake-up event will occur the next time that hour passes. Once it has, or the device wakes up from movement, the default wake-up times will be used.

**Set Sleep Timer**  
*Command setsleep5*  
Set the time in minutes until the device should enter deep sleep. The number after *setsleep* is the number of minutes you want to set the timer to (1 or greater).

**Idle**  
*Command: idle  
Alternative command: idle12*  
Put the device in an idle mode where the device is still on but nothing runs except the GSM module (although at low power). Does not save as much battery as the default deep sleep mode, but it can instead still receive SMS-commands. If the command is followed by a number between 1 and 24 (24-hour clock, 24 is midnight), the wake-up event will occur the next time that hour passes. Once it has, or the device wakes up from movement, the default wake-up times will be used.

**Stop idle mode**  
*Command: stopidle*  
Use this command to exit *idle* mode and use the default *deep sleep* instead.

**Battery Check**  
*Command: battery*  
Check the device’s battery.

**Uptime**  
*Command: uptime*  
Check how long the device has been active without a reset.

**Shut-down**  
*Comand: shutdown*  
Set the device in idle mode without movement interrupts waking up the device. The device will automatically wake up after 2 hours.

**Startup**  
*Command: startup*  
Wake the device up after having been in a shut-down state.

## Setup
When preparing the software for upload to the board, the arduino_secrets.h file needs to be set up with the following variables (download the file from GitHub for ease of setup, it needs to be in the same project directory as the main .ino file):  
```
#define SECRET_PINNUMBER "SIM-card pin number"
#define SECRET_PHONENUMBER "phone number you want to send SMS-messages to"
#define SECRET_PASSWORD "SMS commands password"
#define SECRET_GPRS_APN "your mobile provider’s APN address"
#define SECRET_GPRS_LOGIN "the APN login"
#define SECRET_GPRS_PASSWORD "the APN password"
```

## Mounting on the bike
I’ve had several different ideas on how to mount the tracker on a bike. Commercial trackers can often be hidden in the handlebars or similar, but the MKR GSM 1400 module can be a bit too wide for that with its 25 millimeters. Other options could be in the seat-post hole or in a case mounted to the frame somewhere.

For version 2.0 of the tracker I have used a combination of putting the tracker inside the frame and on the frame.

### A mix
For the final iteration of the tracker I've opted to do a mix of hiding the tracker inside the frame and on it. The main board and batteries are inside the seat tube and anything with an antenna is mounted under the saddle.

On my bicycle the seat post is 27 mm in diameter, and the board is 25 mm wide. Perfect. To mount the board inside the post I cut two grooves in the end of the post and made a whole where I could put a bolt and nut to keep the board in place.
![Seat post groove](/images/seat_post_01.jpg)

It was also necessary to drill a hole at the top of the post, in the saddle mount, to be able to pull cables up to the underside of the saddle.
![Seat post cable hole](/images/seat_post_02.jpg)

To accomodate the new placement the batteries needed to be placed one after the other, which is why I opted to use two single 18650 Li-Po cell holders that I connected in parallell. The cells were taken from a 4400mAh battery pack, which also meant that I got a battery control circuit for free. Be careful if you're going down this route... Connecting the batteries or control circuit wrong can very easily result in your house burning down. Only attempt this if you know what you're doing. If you don't know what you're doing use ready-made batteries (but these might not fit in a regular battery holder and the charging might work differently).
![Battery control circuit](/images/battery_circuit.jpg)

Here's a general overview of the circuit before mounting everything.
![General circiut overview](/images/saddle_overview.jpg)

Getting everything in place wasn't entirely straightforward. The battery holders needed to be cut down to fit inside the seat post and pulling all the cables through took a few tries (using a bent piece of metal wire).
![Wire management](/images/mounting_wirepull_02.jpg)
In the above image you can also see that I'm covering up the main board with shrink tube, to protect it when sliding it into the seat post (the same is true for other components, but more on that below).
![Top of seat post, with wires](/images/mounting_wirepull_01.jpg)

GPS module, charging circuit, GSM antenna and on/off-switch are all mounted under the saddle. For this I made a custom bracket for the antenna and on/off-switch and mounted charging circuit and GPS module in shrink wrap and an old bicycle tube respectively before attaching everything with screws directly to the underside of the saddle (make sure the screws are short enough to not pierce the saddel when you sit on it).
![Mounting bracket](/images/bracket.jpg)
![Everything mounted under the saddle](/images/placement.jpg)

Finally everything is covered with a piece of waterproof cloth that I had laying around. The back of the cloth is not attached to allow the charging cable to be pulled out for charging the unit.
![Covered with waterproof cloth](/images/)

The finished unit is pretty well hidden.
![The final result](/images/finished_01.jpg)
In the above image you can also see the accelerometer hanging loose underneath the seat post. This makes it more sensitive to movement. And here's a closeup.
![The final result - closeup](/images/finished_02.jpg)

#### Advantages
More difficult to detect, but still very accessible. 

#### Drawbacks
Since the tracker is integrated into the seat post, simply removing the saddle and seat post would make the tracker useless.

### Inside the frame
When hiding the tracker in the frame it’ll be necessary to construct it in a tube-like fashion. All the components need to be laid out in a line and the battery needs to be single-cell or two cells laid out one after the other rather than next to each other.

If the handlebars are wide enough to fit the tracker that’s great. It’ll be relatively easy to access and can be well hidden.

Other options for hiding the tracker inside the frame pretty much only includes in/under the seat post. But, the tracker will be difficult to get too if it needs to be turned off, charged, updated, etc.

One thing to keep in mind when planning to put the tracker inside the frame is the possibly detrimental effect it will have on reception, both for the GSM module and the GPS shield. Putting the antenna inside a metal tube isn’t the best option if you want a strong signal...

#### Advantages
Well hidden.

#### Drawbacks
Well hidden. It can be hard to get to the tracker if necessary. Reception migth suffer.

### On the frame
If hiding the tracker inside the frame isn’t an option, putting it on the frame somewhere is the only way. Hiding it under the seat might be possible, in some kind of box you bolt/strap to the frame, etc.

For the first version of the tracker (the proof of concept, pretty much) I opted for a solution where I mounted a the tracker on the frame. To make this somewhat hidden I merged the box with the mounting holder for my bicycle lock. That way the box kind of looks like it is part of the mount, but it is still easily accessible. For my prototype I used a multipurpose plastic enclosure from Hammond Electronics (1591HBK) that I (heavily) modified to accommodate the lock mount and to also make it somewhat waterproof. It took a lot of cutting, grinding, gluing and swearing, but eventually I got it to work like I want it to (although it's not pretty). If I had access to a 3D-printer I would have designed a more streamlined case that doesn’t stick out like a sore thumb, but that’s for another time.

Below you see the finished case that I ended up using.
![Finished case](/images/case.jpg)

Here it is mounted on the bike.
![Mounted case](/images/case_mounted.jpg)

And a comparison with an unmodified bike lock mount.
![Original lock mount for comparison](/images/case_compare.jpg)

Lastly, a view of the insides of the case (the main board and the LiPo charger can just about be discerned behind the green GSM antenna).
![Internal wiring and electronics](/images/case_internal.jpg)

The weatherproofing comes from sealing holes and seams with a whole lot of rubber glue and cutting out gaskets for the lid from a bicycle wheel inner tube.

#### Advantages
The tracker is easily accessible.

#### Drawbacks
The tracker is easily accessible. When everything is mounted on the frame it is quite easy to find for any would-be thief.

## Customising alerts
The SMS alerts are constructed so that you can use an automation tool (like [Tasker](https://play.google.com/store/apps/details?id=net.dinglisch.android.taskerm) or similar) to filter out and create notification sounds for the SMS alerts of your choice. There is a simple [Tasker](https://play.google.com/store/apps/details?id=net.dinglisch.android.taskerm) profile [here](Bike_Tracker_Movement_Alert.prf.xml) that you can use as a base. Sorry iOS people, I'm not quite sure what you could use for the same effect. Maybe ITTT or Workflow?

An example on how the SMS alerts are constructed is that when movement interrupts a deep sleep cycle the SMS will start with "Waking up", but if the wake-up event is caused by the RTC the message will start with "Scheduled wake-up". This is because you might not want to get a notification sound for a scheduled wake-up, but for a movement triggered wake-up you do.

## Licence
MIT License

Copyright (c) 2021 Johan Oscarsson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
