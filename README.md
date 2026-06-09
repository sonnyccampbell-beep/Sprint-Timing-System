# Sprint-Timing-System
Affordable and accurate DIY timing system for sprinting. Utilising ESP-32 microcontrollers and E18-D80NK IR sensors, this system can time training reps to a high degree of accuracy. There are two start modes: startgun and flying start. These can be toggled using the sole button on the starter module. 

The current setup includes 3 modules in total, 1 of these is the controller module that is used to start reps and change timing modes, the other two are the sensing modules that will detect and display the times. Theoretically, an infinite number of timing modules could be added, the code would just have to be updated to include the new MAC Adresses of the microcontrollers in the communication between the modules.

