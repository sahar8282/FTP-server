# Computer Network Design & Application Template 
This project implements a server using C++ that handles client connections and performs various operations such as file management and client interaction. The server is designed to use POSIX threads and relies on the C++ Standard Library.


# Features

Multithreaded handling of client connections.

# File management commands:

LIST: Lists files in the server's directory.

GET: Retrieves a file from the server.

PUT: Uploads a file to the server.

DEL: Deletes a file from the server.

User authentication using a password file.

Directory-based file management.

### To compile project

--> g++ -std=c++17 server.cpp -o server -pthread 

# To run the project

Run the compiled server binary with the required arguments:

./server -d directory -p port -u password_file

directory: The server's working directory for file operations.

port: The port number on which the server will listen for client connections.

password_file: A file containing client credentials.

# Supported Commands
--> USER username password
(username and password should exist in the password.cfg)     

--> LIST      

--> GET filename      

--> PUT filename     

--> DEL filename        
                                                                                                                                            
--> QUIT

# To connect a client from local host 

-->nc localhost port





