#include <iostream>

#include <DirManager/dirman.h>

int main(int , char *[])
{
    DirMan myDir("../");

    std::cout << "=============Running test 1 (absolue path from relative)=============" << std::endl;
    std::cout << myDir.absolutePath() << std::endl;

    std::cout << "=============Running test 2 (list of files in folder)=============" << std::endl;
    std::vector<std::string> files;
    myDir.getListOfFiles(files);

    for(std::string &file : files)
        std::cout << file << std::endl;

    std::cout << "=============Running test 3 (walk in the subdirectories)=============" << std::endl;
    std::vector<std::string> filters;
    filters.push_back(".txt");

    myDir.beginWalking(filters);

    std::string itPath;
    while(myDir.fetchListFromWalker(itPath, files))
    {
        for(std::string &file : files)
            std::cout << itPath + "/" + file << std::endl;
        std::cout.flush();
    }

    std::cout << "=============Running test 4 (Create and delete single directory)=============" << std::endl;
    if(myDir.mkdir("Directory which must not exist!!!"))
        std::cout << "mkdir Ok!" << std::endl;
    else
        std::cout << "mkdir FAILED!" << std::endl;

    if(myDir.rmdir("Directory which must not exist!!!"))
        std::cout << "rmdir Ok!" << std::endl;
    else
        std::cout << "rmdir FAILED!" << std::endl;

    std::cout << "=============Running test 5 (Create and delete directory treee)=============" << std::endl;
    if(myDir.mkpath("Directory which must not exist!!!/wat/this is a true crap which must not exist!"))
        std::cout << "mkpath Ok!" << std::endl;
    else
        std::cout << "mkpath FAILED!" << std::endl;

    myDir.mkdir("Directory which must not exist!!!/xxx");
    myDir.mkpath("Directory which must not exist!!!/some/shit");

    {
        // Create a dummy file (may fail on the Windows if full path is non-ASCII)
        FILE *shit = fopen((myDir.absolutePath() + "/Directory which must not exist!!!/shit.txt").c_str(), "w");
        if(shit)
        {
            fprintf(shit, "boobooboobooboo");
            fclose(shit);
        }
    }

    if(myDir.rmpath("Directory which must not exist!!!"))
        std::cout << "rmpath Ok!" << std::endl;
    else
        std::cout << "rmpath FAILED!" << std::endl;

    return 0;
}
