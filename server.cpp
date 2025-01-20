/*****************************************************************************/
/*** An echo server using threads.                                         ***/
/*** Compile : g++ -std=c++17 server.cpp -o server -pthread                 ***/
/*** run: ./server -d directory -p 1508 -u password_file                   ***/
/*****************************************************************************/

#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <vector>
#include <thread>
#include <string.h>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <mutex>
#include <fcntl.h>
#include <algorithm>
#include <filesystem>
#define PORT 1508
#define DEFAULT_BUFLEN 1024
using namespace std;

bool serverRunning = true;
int serverSocket;
const int MAX_CLIENTS = 100;
string directory;
mutex clientMutex;
vector<int> clientSockets;
void handleListCommand(int clientSocket);
void handleGetCommand(int clientSocket, const string &message);
void handlePutCommand(int clientSocket, string &message);
void handleDelCommand(int clientSocket, string &message);
void disconnectClient(int clientSocket);

void usageError(const char *programName)
{
    cerr << "use this command: " << programName << " -d <value> -p <value> -u <value>" << endl;
    cerr << "  -d <value>  Specify running directory which files to be accessed/modified/erased" << endl;
    cerr << "  -p <value>  Define server port number" << endl;
    cerr << "  -u <value>  Password file that uses delimiter separated format which is delimiter is ':'" << endl;
}

//function to load credentials from a file and return them as a map.
unordered_map<string, string> loadCredentials(const string &filePath)
{
    unordered_map<string, string> credentials;
    ifstream file(filePath);

    if (!file.is_open())
    {
        throw runtime_error("Error: Failed to open password file: " + filePath);
    }

    string line;
    while (getline(file, line))
    {
        istringstream iss(line);
        string username, password;

        // Read username and password separated by a colon
        if (getline(iss, username, ':') && getline(iss, password))
        {
            credentials[username] = password;
        }
    }

    return credentials;
}

// function to set the given socket to non-blocking mode.
 
void setNonBlocking(int socket)
{
    int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

//function to send a message to a specified client socket.
ssize_t sendMessage(int clientSocket, const string &message)
{
    ssize_t bytesSent = send(clientSocket, message.c_str(), message.size(), 0);
    if (bytesSent == -1)
    {
        cerr << "Error: Failed to send message to client." << endl;
    }
    return bytesSent;
}

//function to handle the SIGINT signal.


void signalHandler(int signal)
{
    if (signal == SIGINT)
    {
        cout << "Received SIGINT signal. Shutting down server..." << endl;

        // Close all client sockets
        lock_guard<mutex> lock(clientMutex);
        for (int socket : clientSockets)
        {
            sendMessage(socket, "Server is shutting down. Goodbye!\n");
            close(socket);
        }
        clientSockets.clear();

        serverRunning = false;
    }
}

//function to set up a server socket to listen for incoming connections on the specified port.

int setupServer(int port)
{
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        throw runtime_error("Error: Failed to create socket.");
    }
    cout << "Server socket created successfully." << endl;

    // Bind the socket to the port
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        close(serverSocket);
        throw runtime_error("Error: Failed to bind socket to port " + to_string(port) + ".");
    }
    cout << "Socket successfully bound to port " << port << "." << endl;

    // Start listening for connections
    if (listen(serverSocket, 5) == -1)
    {
        close(serverSocket);
        throw runtime_error("Error: Failed to listen on socket.");
    }

    // Set the server socket to non-blocking mode
    setNonBlocking(serverSocket);

    return serverSocket;
}


string toUpper(string str)
{
    transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}


//function to handle communication with a connected client.
void handleClient(int clientSocket, const unordered_map<string, string> &credentials)
{
    sendMessage(clientSocket, "Welcome to Sahar's file server.\n");

    char buffer[DEFAULT_BUFLEN];
    ssize_t bytesRead;

    bool isAuthenticated = false;
    string loggedInUser;
    int failedAttempts = 0;

    while (serverRunning)
    {
        memset(buffer, 0, DEFAULT_BUFLEN); // Clear the buffer before reading

        bytesRead = recv(clientSocket, buffer, DEFAULT_BUFLEN - 1, 0);

        if (bytesRead == 0)
        {
            cout << "Client disconnected." << endl;
            break;
        }
        else if (bytesRead == -1)
        {
            if (errno == EWOULDBLOCK)
            {
                continue;
            }
            cerr << "Error: Failed to read from client socket." << endl;
            break;
        }

        string message = string(buffer);

        // Print the received message
        cout << "Received message: " << message << endl;

        if (message == "QUIT\n")
        {
            sendMessage(clientSocket, "Goodbye!\n");
            cout << "Client disconnected." << endl;
            break;
        }

        if (isAuthenticated)
        {
            if (toUpper(message) == "PING\n")
            {
                sendMessage(clientSocket, "PONG\n");
                cout << "PONG sent." << endl;
            }
            else if (toUpper(message) == "LIST\n")
            {
                handleListCommand(clientSocket);
            }
            else if (toUpper(message.substr(0, 3)) == "GET")
            {
                handleGetCommand(clientSocket, message);
            }
            else if (toUpper(message.substr(0, 3)) == "PUT")
            {
                handlePutCommand(clientSocket, message);
            }
            else if (toUpper(message.substr(0, 3)) == "DEL")
            {
                handleDelCommand(clientSocket, message);
            }
            else
            {
                sendMessage(clientSocket, "400 Invalid command.\n");
                cout << "Invalid command received." << endl;
            }
        }
        else
        {
            istringstream iss(message);
            string command, username, password;

            iss >> command >> username >> password;

            if (command == "USER")
            {
                if (username.empty() || password.empty())
                {
                    sendMessage(clientSocket, "400 Invalid format. Use: USER <username> <password>\n");
                    cout << "Invalid login format received." << endl;
                }
                else
                {
                    auto iterator = credentials.find(username);
                    if (iterator != credentials.end() && iterator->second == password)
                    {
                        isAuthenticated = true;
                        loggedInUser = username;
                        sendMessage(clientSocket, "200 User " + username + " granted to access.\n");
                        cout << "User " << username << " authenticated." << endl;
                    }
                    else
                    {
                        sendMessage(clientSocket, "400 User not found. Please try with another user.\n");
                        failedAttempts++;
                        cout << "User " << username << " not found." << endl;
                    }
                }
            }
            else
            {
                sendMessage(clientSocket, "401 Unauthorized access. Please login first using USER <username> <password>.\n");
                cout << "Unauthorized access. Please login first." << endl;
            }

            if (failedAttempts >= 3)
            {
                sendMessage(clientSocket, "ERROR: Too many failed login attempts. Closing connection.\n");
                cout << "Too many failed login attempts. Closing connection." << endl;
                break;
            }
        }
    }

    // Close the client socket
    disconnectClient(clientSocket);
}

//function to disconnect a client and remove its socket from the list of client sockets.
void disconnectClient(int clientSocket)
{
    lock_guard<mutex> lock(clientMutex);
    auto iterator = find(clientSockets.begin(), clientSockets.end(), clientSocket);
    if (iterator != clientSockets.end())
    {
        clientSockets.erase(iterator);
    }
    close(clientSocket);
}
//function to be called when the client sends a LIST command. 
void handleListCommand(int clientSocket)
{
    stringstream response;

    for (const auto &entry : filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file())
        {
            response << entry.path().filename().string() << " - " << entry.file_size() << endl;
        }
    }

    response << "." << endl;

    sendMessage(clientSocket, response.str());
}

//function to be called when the client sends a GET command.
void handleGetCommand(int clientSocket, const string &message)
{
    istringstream iss(message);
    string command, filename;

    iss >> command >> filename;

    if (filename.empty())
    {
        sendMessage(clientSocket, "400 Invalid command. Use: GET <filename>\n");
        cout << "Invalid GET command received." << endl;
        return;
    }

    string filePath = directory + "/" + filename;

    if (!filesystem::exists(filePath))
    {
        sendMessage(clientSocket, "404 File " + filename + " not found.\n");
        cout << "File not found: " << filename << endl;
        return;
    }

    ifstream file(filePath, ios::binary);
    if (!file.is_open())
    {
        sendMessage(clientSocket, "500 Internal server error.\n");
        cerr << "Error: Failed to open file: " << filename << endl;
        return;
    }

    stringstream response;
    response << file.rdbuf();
    response << endl
             << "." << endl;

    sendMessage(clientSocket, response.str());
    cout << "File " << filename << " sent." << endl;
}

//function to be called when the client sends a PUT command.
void handlePutCommand(int clientSocket, string &message)
{
    istringstream iss(message);
    string command, filename;

    iss >> command >> filename;

    if (filename.empty())
    {
        sendMessage(clientSocket, "400 Invalid command. Use: PUT <filename>\n");
        cout << "Invalid PUT command received." << endl;
        return;
    }

    string filePath = directory + "/" + filename;
    ofstream file(filePath, ios::binary);
    if (!file.is_open())
    {
        sendMessage(clientSocket, "400 File can not save on server side.\n");
        cerr << "Error: Failed to open file for writing: " << filename << endl;
        return;
    }

    ssize_t bytesRead;
    char buffer[DEFAULT_BUFLEN];
    size_t totalBytes = 0;

    while (true)
    {
        memset(buffer, 0, DEFAULT_BUFLEN);
        bytesRead = recv(clientSocket, buffer, DEFAULT_BUFLEN - 1, 0);

        if (bytesRead == -1)
        {
            if (errno == EWOULDBLOCK)
            {
                continue;
            }
            cerr << "Error: Failed to read from client socket." << endl;
            file.close();
            sendMessage(clientSocket, "400 File cannot save on server side.\n");
            return;
        }

        string data(buffer, bytesRead);

        if (data == ".\n")
        {
            break;
        }
        else
        {
            file.write(data.c_str(), bytesRead);
            totalBytes += bytesRead;
        }
    }

    file.close();
    sendMessage(clientSocket, "200 " + to_string(totalBytes) + " Byte " + filename + " file retrieved by server and was saved.\n");
    cout << "File " << filename << " saved. " << totalBytes << " bytes transferred." << endl;
}

//function to be called when the client sends a DEL command.
void handleDelCommand(int clientSocket, string &message)
{
    istringstream iss(message);
    string command, filename;

    iss >> command >> filename;

    if (filename.empty())
    {
        sendMessage(clientSocket, "400 Invalid command. Use: DEL <filename>\n");
        cout << "Invalid DEL command received." << endl;
        return;
    }

    string filePath = directory + "/" + filename;

    if (!filesystem::exists(filePath))
    {
        sendMessage(clientSocket, "404 File " + filename + " not on the server.\n");
        cout << "File not found: " << filename << endl;
        return;
    }

    if (remove(filePath.c_str()) != 0)
    {
        sendMessage(clientSocket, "500 Internal server error.\n");
        cerr << "Error: Failed to delete file: " << filename << endl;
        return;
    }

    sendMessage(clientSocket, "200 File " + filename + " deleted.\n");
    cout << "File " << filename << " deleted." << endl;
}


// main function
int main(int argc, char *argv[])
{
    int option;
    int port;
    string passfile;

    // get command line arguments
    while ((option = getopt(argc, argv, "d:p:u:")) != -1)
    {
        switch (option)
        {
        case 'd':
            directory = optarg;
            break;
        case 'p':
            port = stoi(optarg);
            break;
        case 'u':
            passfile = optarg;
            break;

        default:
            usageError(argv[0]);
            return 1;
        }
    }

    // Check if directory is empty or not and the port and password file are entered

    if (directory.empty() || (port < 0 && port > 65535) || passfile.empty())
    {
        cerr << "Error: All arguments -d, -p, and -u should be specified." << endl;
        usageError(argv[0]);
        return 1;
    }

    // validate server port number
    try
    {
        if (port <= 0 || port > 65535)
        {
            cerr << "Error: Invalid port number. port should be between 1 and 65535." << endl;
            return 1;
        }
    }
    catch (const invalid_argument &)
    {
        cerr << "Error: Port number should be an integer." << endl;
        return 1;
    }
    catch (const out_of_range &)
    {
        cerr << "Error: Port number not in the range." << endl;
        return 1;
    }
    
//validating the file existance
     if (!filesystem::exists(directory))
    {
        cerr << "Error: Provided directory does not exist or is invalid.\n";
        return 1;
    }

 // Load credentials from the password file
    unordered_map<string, string> credentials;
    try
    {
        credentials = loadCredentials(passfile);
    }
    catch (const exception &e)
    {
        cerr << e.what() << endl;
        return 1;
    }


     // Setup signal handler
    signal(SIGINT, signalHandler);

    // Create server socket
    try
    {
        serverSocket = setupServer(port);
    }
    catch (const exception &e)
    {
        cerr << e.what() << endl;
        return 1;
    }

    cout << "Server is listening on port " << port << "." << endl;

    
    // Main loop

    fd_set readfds;
    vector<thread> clientThreads;

    while (serverRunning)
    {
        // Clear the set of file descriptors
        FD_ZERO(&readfds);

        // Add the server socket to the set
        FD_SET(serverSocket, &readfds);
        int maxFd = serverSocket;

        // Add client sockets to the set
        {
            lock_guard<mutex> lock(clientMutex);
            for (int socket : clientSockets)
            {
                FD_SET(socket, &readfds);
                maxFd = max(maxFd, socket);
            }
        }

        // Set a timeout of 1 second for select
        timeval timeout{1, 0};

        // Wait for activity on any of the sockets
        int activity = select(maxFd + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0)
        {
            if (errno == EINTR)
            {
                // Interrupted by signal, continue
                continue;
            }
            else
            {
                cerr << "Error: select() failed." << endl;
                break;
            }
        }

        // Check if there is activity on the server socket
        if (!FD_ISSET(serverSocket, &readfds))
        {
            // No activity on the server socket, continue
            continue;
        }

        // Accept incoming connections
        sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &clientAddrLen);
        if (clientSocket == -1)
        {
            if (!serverRunning)
            {
                break;
            }
            cerr << "Error: Failed to accept incoming connection." << endl;
            continue;
        }
        cout << "Accepted incoming connection." << endl;

        // Handle the client connection
        if (clientThreads.size() < MAX_CLIENTS)
        {
            setNonBlocking(clientSocket); // Set the client socket to non-blocking mode
            {
                lock_guard<mutex> lock(clientMutex);
                clientSockets.push_back(clientSocket);
            }

            clientThreads.emplace_back(thread(handleClient, clientSocket, credentials));
        }
        else
        {
            cerr << "Error: Maximum number of client connections reached." << endl;
            close(clientSocket);
        }
    }

// Close the server socket
    close(serverSocket);

    cout << "Server stopped." << endl;

    return 0;
}