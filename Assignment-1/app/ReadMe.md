# A simple MultiThread programming application on Zephyr RTOS.
### Author: Ashish kumar rambhatla

The following sections will demonstrate how to compile and run the program.


##   SYSTEM REQUIREMENTS  ##

- Runs on macos (12.2.1).

- Requires Zephyr source code. 

- Requires JLink Software package. 

## RUNNING APPLICATION ##

1. Navigate to the Application directory (ROOT_DIR).

2. Source the Zephyr environment file using the zephyr-env.sh present in the zephyr source tree.
    i) $ source zephyrproject/zephyr/zephyr-env.sh

3. Now build the source directory using the following command. 
    i) $ west build -p auto.

4. After succesful compilation, flash the obtained executable using the below command. Make sure
   the target configured and connected succesfully before proceeding to the flashing step.
    i) $ west flash.

5. Now the board is running the program and will prompt a uart shell on target. Use the following
   command to activate the program.
   i) uart:~$ activate.

6. The program will be triggered on executing the above command and will finish just in 4 seconds as 
   configred in its settings.
