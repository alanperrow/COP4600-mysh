/*
 * Alan Perrow
 * 02/06/2021
 * COP4600 Boloni
 * UCF Spring 2021
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <dirent.h>
#include <errno.h>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
using namespace std;

#ifdef _WIN32
    #define PATH_SEP '\\'
#endif
#ifndef PATH_SEP
    #define PATH_SEP '/'
#endif

int cmdLoop();
string pathAppend(const string &, const string &);
string getNewPath(const string &, const string &);
bool numReqArgsIsMet(int, string);
int moveToDir(string &, string &);
int whereAmI(string &);
int history(bool);
string strfyTokens(const vector <string> &);
string replay(string);
bool isDigits(const string &);
int start(const string &, const vector <string> &);
int background(const string &, const vector <string> &);
int dalek(const string &);
int dalek(const pid_t);
int dalekall();

/*
 * Handles command info.
 * Instantiate to create a new command with a name and description of its arguments and their usage.
 */
class Command {
    public:
        string name;            // Command name
        string desc;            // Command description
        vector <string> argdesc;// Description of each argument
        vector <bool> argreq;   // Is argument required?
        int num_reqargs;        // Number of required arguments
        string usage;           // Command usage (auto-generated by constructor)

        /* Constructor */
        Command(string _name, string _desc, vector <string> _argdesc, vector <bool> _argreq, int _num_reqargs) {
            // Generate usage string
            name = _name;
            desc = _desc;
            argdesc = _argdesc;
            argreq = _argreq;
            num_reqargs = _num_reqargs;
            usage.append("Usage: ").append(name);
            for (int i=0; i<argdesc.size(); i++) {
                if (argreq[i]) {
                    usage.append(" <").append(argdesc[i].append(">"));
                } else {
                    usage.append(" [").append(argdesc[i].append("]"));
                }
            }
            usage.append("\n\t").append(desc);
        }

        string getUsage() {
            return usage;
        }
};


string currentDir = ".";
vector <Command> commandList;
map <string, int> commandMap;
vector <string> commandHistory;
vector <pid_t> pidList;

/*
 * Initialize shell.
 */
int main(int argc, char* argv[]) {
    cout << "Alan Perrow - HW3: MyShell" << endl;

    // Define all required commands and associate each with their respective index
    commandList.push_back( Command("movetodir", "Changes the current directory.",
                                    {"directory"}, {true}, 1) );
    commandMap["movetodir"] = 0;
    commandList.push_back( Command("whereami", "Prints the current directory.",
                                    {}, {}, 0) );
    commandMap["whereami"] = 1;
    commandList.push_back( Command("history", "Prints recently typed commands.\n\t-c:\tclears history.",
                                    {"-c"}, {false}, 0) );
    commandMap["history"] = 2;
    commandList.push_back( Command("byebye", "Terminates the shell.",
                                    {}, {}, 0) );
    commandMap["byebye"] = 3;
    commandList.push_back( Command("replay", "Re-executes the command labeled with some number in history.",
                                    {"number"}, {true}, 1) );
    commandMap["replay"] = 4;
    commandList.push_back( Command("start", "Executes the program found at the path provided with given parameters, if any.",
                                    {"program", "parameters"}, {true, false}, 1) );
    commandMap["start"] = 5;
    commandList.push_back( Command("background", "Executes the program found at the path provided with given parameters, if any.\n\tPrints PID of the started program and returns the prompt.",
                                    {"program", "parameters"}, {true, false}, 1) );
    commandMap["background"] = 6;
    commandList.push_back( Command("dalek", "Terminates the program with a specified PID.",
                                    {"PID"}, {true}, 1) );
    commandMap["dalek"] = 7;
    // Extra credit commands
    commandList.push_back( Command("dalekall", "Terminates all programs previously started by this shell which have not already been terminated.",
                                    {}, {}, 0) );
    commandMap["dalekall"] = 8;

    // Load previous history from log file
    ifstream fsiHistory("history.log");
    string fsiHistoryLine;
    if (fsiHistory.is_open()) {
        while (getline(fsiHistory, fsiHistoryLine)) {
            commandHistory.push_back(fsiHistoryLine);
        }
        fsiHistory.close();
    }

    int res = cmdLoop();

    // Save history to log file
    ofstream fsoHistory("history.log");
    if (fsoHistory.is_open()) {
        for (int i=0; i<commandHistory.size(); i++) {
            fsoHistory << commandHistory[i] << endl;
        }
        fsoHistory.close();
    } else {
        perror("Error");
    }

    return res;
}

/*
 * Get command inputs until shell ends.
 * Returns 0 if ended normally, 1 otherwise.
 */
int cmdLoop() {

    // Command loop
    bool done=false, flag=false, saveToHistory, replaying=false;
    string replayLine;

    while (!done) {
        string line;
        if (!replaying) {
            // Get new line command
            cout << "# ";
            getline(cin, line);
        } else {
            // Get line command set by replay
            line = replayLine;
            replaying = false;
        }

        // Tokenize input
        string token;
        vector <string> tokens;
        size_t pos = 0;
        size_t nextPos;

        while ((nextPos = line.find(" ", pos)) != string::npos) {
            token = line.substr(pos, nextPos-pos);
            if (token.find_first_not_of(" ") != string::npos) {
                // Only tokenize if not whitespace
                tokens.push_back(token);
            }
            pos = nextPos + 1;
        }
        token = line.substr(pos);
        if (token.find_first_not_of(" ") == string::npos || token.empty()) {
            // Leftover token was only whitespace/empty; do not tokenize
            if (tokens.size() == 0) {
                // No tokens in this entire line; do not save in history
                continue;
            }
        } else {
            tokens.push_back(token);
        }

        // Perform command, if valid
        saveToHistory = true;
        if (tokens[0] == "movetodir") {
            if (numReqArgsIsMet(tokens.size(), "movetodir")) {
                moveToDir(currentDir, tokens[1]);
            }
        }
        else if (tokens[0] == "whereami") {
            if (numReqArgsIsMet(tokens.size(), "whereami")) {
                whereAmI(currentDir);
            }
        }
        else if (tokens[0] == "history") {
            if (numReqArgsIsMet(tokens.size(), "history")) {
                saveToHistory = false;
                if (tokens.size() > 1) {
                    // Check if we passed optional "clear" specifier.
                    history(tokens[1] == "-c");
                } else {
                    // Save this command to history before calling the function.
                    // Otherwise the list will be printed out and then added to, making everything off by 1.
                    commandHistory.push_back(strfyTokens(tokens));
                    history(false);
                }
            }
        }
        else if (tokens[0] == "byebye") {
            done = true;
        }
        else if (tokens[0] == "replay") {
            if (numReqArgsIsMet(tokens.size(), "replay")) {
                string res = replay(tokens[1]);
                if (!res.empty()) {
                    // Buffer a command to be executed next loop
                    replayLine = res;
                    replaying = true;
                    continue;
                }
            }
        }
        else if (tokens[0] == "start") {
            if (numReqArgsIsMet(tokens.size(), "start")) {
                vector <string> params;
                for (int i=2; i<tokens.size(); i++) {
                    // Store any extra parameters
                    params.push_back(tokens[i]);
                }
                start(tokens[1], params);
            }
        }
        else if (tokens[0] == "background") {
            if (numReqArgsIsMet(tokens.size(), "background")) {
                vector <string> params;
                for (int i=2; i<tokens.size(); i++) {
                    // Store any extra parameters
                    params.push_back(tokens[i]);
                }
                background(tokens[1], params);
            }
        }
        else if (tokens[0] == "dalek") {
            if (numReqArgsIsMet(tokens.size(), "dalek")) {
                int res = dalek(tokens[1]);
                if (res == 0) {
                    cout << "Terminated process with pid: " << tokens[1] << endl;
                } else if (res == -1) {
                    cout << "Failed to terminate process with pid: " << tokens[1] << endl;
                    perror("Error");
                } else {
                    cout << tokens[1] << " is not a valid non-negative integer pid." << endl;
                }
            }
        }
        else if (tokens[0] == "dalekall") {
            if (numReqArgsIsMet(tokens.size(), "dalekall")) {
                dalekall();
            }
        }
        else {
            cout << "\"" << tokens[0] << "\" is not a recognized command." << endl;
        }

        // Save command to history
        if (saveToHistory) { commandHistory.push_back(strfyTokens(tokens)); }
    }
    if (flag) { return 1; }
    return 0;
}

/*
 * Returns true if required number of args for a command is satisfied, false otherwise.
 * Prints error and correct usage if not enough args provided.
 */
bool numReqArgsIsMet(int tokensSize, string commandStr) {
    int ind = commandMap.at(commandStr);   // Get commandList index
    bool res = tokensSize-1 >= commandList[ind].num_reqargs;
    if (!res) {
        cout << "Not enough arguments provided.\n" << commandList[ind].getUsage() << endl;
    }
    return res;
}

/*
 * Appends two paths together, inserting a seperator if necessary.
 * Returns appended string.
 */
string pathAppend(const string &p1, const string &p2) {
    string tmp = p1;
    if (p1[p1.length()] != PATH_SEP) {
        // Need to add a path separator
        tmp += PATH_SEP;
        return(tmp + p2);
    }
    return(p1 + p2);
}

/*
 * If path begins with ${PATH_SEP}, assumes it is absolute and so returns path.
 * Otherwise, appends path to currentDir and returns it.
 * Assumes path has size > 0.
 */
string getNewPath(const string &currentDir, const string &path) {
    if (path[0] == PATH_SEP) {
        return path;
    }
    return pathAppend(currentDir, path);
}

/*
 * Updates currentDir (passed by reference) with a new directory path string.
 * Returns 0 if successful, 1 otherwise.
 */
int moveToDir(string &currentDir, string &newDirStr) {
    string path = getNewPath(currentDir, newDirStr);
    DIR *dir = opendir( realpath(path.c_str(), NULL) ); // Open directory stream
    if (dir == NULL) {
        // Could not open directory; print reason why
        perror("Error");
        return 1;
    }
    currentDir = path;
    closedir(dir); // Close directory stream
    return 0;
}

/*
 * Prints currentDir (passed by reference).
 * Returns 0 if successful, 1 otherwise.
 */
int whereAmI(string &currentDir) {
    cout << "Current directory: \"" << currentDir << "\"" << endl;
    char *absPath = realpath(currentDir.c_str(), NULL);
    cout << "Absolute: \"" << absPath << "\"" << endl;
    return 0;

    /* SHOW DIRECTORY CONTENTS
    struct dirent *ent;
    DIR *dir = opendir(currentDir.c_str()); // Open directory stream
    if (dir == NULL) {
        perror("Error");
        return 1;
    }
    
    cout << "Directory stream in \"" << currentDir << "\":" << endl;
    while ((ent = readdir(dir)) != NULL) {
        cout << "| " << ent->d_name << endl;
    }

    closedir(dir); // Close directory stream
    return 0;
    */
}

/*
 * Prints out past typed commands in reverse order. If "-c" is passed, clear this history.
 * Returns 0 if successful, 1 otherwise.
 */
int history(bool doClear) {
    if (doClear) {
        commandHistory.clear();
        return 0;
    }
    int last = commandHistory.size()-1;
    for (int i=0; i<=last; i++) {
        cout << i << "| " << commandHistory[last-i] << endl;
    }
    return 0;
}

/*
 * Turns the tokens vector (passed by reference) into a string seperated by whitespace.
 * We don't want to just use the entire line string as there might be unnecessary whitespacing.
 * Returns the stringified tokens vector.
 */
string strfyTokens(const vector <string> &tokens) {
    string str;
    for (int i=0; i<tokens.size(); i++) {
        str.append(tokens[i]).append(" ");
    }
    return str;
}

/*
 * Finds command with specified index in commandHistory.
 * Returns command string if successful, otherwise returns empty string.
 * Also prints error if unsuccessful.
 */
string replay(string index) {
    if (isDigits(index)) {
        int indexInt = stoi(index);
        int last = commandHistory.size()-1;
        if (indexInt < 0 || indexInt > last) {
            cout << indexInt << " is out of bounds of current history." << endl;
            return "";
        }
        return commandHistory[last-indexInt];
    }
    cout << index << " is not a valid non-negative integer index." << endl;
    return "";
}

/*
 * Returns true if a string contains only digits, false otherwise.
 */
bool isDigits(const string &str) {
    return str.find_first_not_of("0123456789") == string::npos;
}

/*
 * Executes a program with given parameters (if any) and waits for it to die.
 * Returns 0 if successful, 1 otherwise.
 */
int start(const string &program, const vector <string> &params) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("Error");
        return 1;
    }
    else if (pid == 0) {
        // Child process
        // Create args array for exec
        int numParams = params.size();
        char *args[2+numParams];
        // Define our destination path
        string path = getNewPath(currentDir, program);

        args[0] = realpath(path.c_str(), NULL);    // Ensure we use absolute path
        for (int i=1; i<=numParams; i++) {
            args[i] = strdup( params[i-1].c_str() );
        }
        args[numParams+1] = NULL;

        // Execute program
        execv(args[0], args);
        perror("Error");
        exit(1);
    }
    else {
        // Parent process
        pid_t gotpid = waitpid(pid, NULL, 0);   // Wait for child to exit
    }
    return 0;
}

/*
 * Executes a program with given parameters (if any) and prints its PID.
 * Adds pid of program to pidList.
 * Returns executed program PID.
 */
int background(const string &program, const vector <string> &params) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("Error");
    }
    else if (pid == 0) {
        // Child process
        // Create args array for exec
        int numParams = params.size();
        char *args[2+numParams];
        string path = getNewPath(currentDir, program);

        args[0] = realpath(path.c_str(), NULL);    // Ensure we use absolute path
        for (int i=1; i<=numParams; i++) {
            args[i] = strdup( params[i-1].c_str() );
        }
        args[numParams+1] = NULL;

        // Execute program
        execv(args[0], args);
        perror("Error");
        exit(1);
    }
    else {
        // Parent process
        cout << "Program started with pid: " << pid << endl;
        pidList.push_back(pid);
    }
    return pid;
}

/*
 * Terminates a process with a specified pid string.
 * Returns value of kill if successful, 1 otherwise.
 */
int dalek(const string &pidStr) {
    if (isDigits(pidStr)) {
        pid_t pid = stoi(pidStr);
        int res = kill(pid, SIGKILL);
        
        // If kill is successful, find pid in pidList and remove it
        if (res == 0) {
            for (int i=0; i<pidList.size(); i++) {
                if (pidList[i] == pid) {
                    pidList.erase(pidList.begin() + i);
                }
            }
        }
        return res;
    }
    return 1;
}

/*
 * Terminates a process with a specified pid.
 * Returns value of kill.
 */
int dalek(const pid_t pid) {
    int res = kill(pid, SIGKILL);
    if (res == 0) {
        for (int i=0; i<pidList.size(); i++) {
            if (pidList[i] == pid) {
                pidList.erase(pidList.begin() + i);
            }
        }
    }
    return res;
}

/*
 * Kills all programs started by this shell by going through each pid stored in pidList.
 * Returns 0 if successful, 1 if any errors were encountered.
 */
int dalekall() {
    pid_t pid;
    int res;
    for (int i=0; i<pidList.size();) {
        pid = pidList[i];
        int _res = dalek(pid);
        if (_res == 0) {
            cout << "Terminated process with pid: " << pid << endl;
        } else if (_res == -1) {
            cout << "Failed to terminate process with pid: " << pid << endl;
            perror("Error");
            i++;    // increment only if we didn't delete an item from pidList
        }
        res += _res;
    }
    return res == 0 ? 0 : 1;
}