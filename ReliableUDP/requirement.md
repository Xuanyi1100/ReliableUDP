
 
allows reliableUDP.cpp to  send a file of the user’s choosing via UDP. Make sure both binary (images, for example) and ASCII (text, for example) files may be transmitted.
 
•   Your code must be developed in C++ ON WINDOWS.
•   You will have to develop a brief protocol that transmits necessary file information and the contents of the file itself.
•   You MUST implement a method of determining that the WHOLE file was correctly transferred (you may choose your method – CRC / LRC / MD5 etc.) You may use publicly available code (be sure to credit the origin of the code if you do so).
•   You may choose a GUI or command line interface.
•   No hard-coded parameters (i.e. IP address, port). You must include reasonable defaults.
•   You MUST devise and demonstrate a test that shows that your whole-file error detection works. This test must be integrated into your software (i.e. you may not manually interrupt communications)
•   Your code should accurately and automatically calculate the transmission time. Please ensure that you can measure fractional seconds accurately. Based on the transmission time and the file size, calculate and display transfer speed IN MEGABITS PER SECOND when the transfer is completed. Hand timing is not acceptable.
 
 
In ReliableUDP.cpp, thoroughly document the places where you will have to add to or make changes to the code. These include, but are not limited to:
•   Retrieving additional command line arguments
•   Client tasks:
o   Retrieving the file from disk
o   Sending file metadata
o   Breaking the file in pieces to send
o   Sending the pieces
•   Server tasks:
o   Receiving the file metadata
o   Receiving the file pieces
o   Writing the pieces out to disk
o   Verifying the file integrity
•   Any other tasks / details you need to take care of
 
Make sure change as little code as possible in reliableudp.cpp, it just transports the hello world now, we will change it to the file transfer. 
Make sure not to change the code in Net.h.
 