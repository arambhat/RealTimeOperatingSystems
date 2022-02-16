# RealTimeOperatingSystems

This Repository hosts the code, reports and ReadMe file of the Assignments of CSE522: Real Time Embedded Systems course.

## Assignment-1
This Assignment deals with creating multiple threads with a set of paramenters such as priority, periodicity, etc., which are defined in a header file called 'task_model.h'. All the spawned threads are required to perform a sequence of compute operations as shown below within their respective deadlines. The total program should be executed in 4000 milliseconds.

           <img width="467" alt="Screen Shot 2022-02-16 at 1 47 12 PM" src="https://user-images.githubusercontent.com/89430730/154354607-4e27754e-4313-4886-bdc0-a9b15c3f1900.png">


-> The mutexes present as part of the compute sequence are configured to have priority ceiling protocol to avoid the priority inversion problem.
-> The ReadMe.txt file will present the system specifications and steps to compile and run the program.
-> The "CSE_522_Assignment_1_Report.pdf" file walks thorugh the implementation details and examines the scheduling patterns using SEGGER systemview.
