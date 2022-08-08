//
// Created by yifan on 22-7-23.
//

#include <iostream>
#include <fstream>
#include <string>
using namespace std;

int main(int argc, char * argv[])
{
    if(argc != 2)
    {
        return 2;
    }

    int num = atoi(argv[1]);
    string path = "traces/";
    path += to_string(num);
    path += ".sh";
    cout << path << endl;
    ifstream fin(path);

    if(!fin.is_open())
    {
        cout << "Cannot open file.\n";
        exit(1);
    }

    string str;

    system("cd ~/code/fslab-handout/ || exit");
    system("make mount || exit");
    system("clear");

    cout << "Testing Point " << num << endl;

//    while(fin.good())
//    {
//        getline(fin, str);
//        if(str.size() == 0)
//            continue;
////        cout << str << endl;
//        system(str.c_str());
//        system("sleep 1s");
//    }

    string cmd = "sh " + path + " || exit";
    system(cmd.c_str());

    cout << "Testing Point " << num << " finished" << endl;

    system("    cd ~/code/fslab-handout/ || exit\n"
           "    make umount || exit");
    system("rm -r vdisk\nmkdir vdisk");

    return 0;
}

/*

for i in $(seq 0 15) ; do
    cd ~/code/fslab-handout/ || exit
    make mount || exit
    clear
    echo "Now Testing Point $i"
    sh traces/$i.sh || exit
    echo "Testing Point $i finished"
    sleep 10s
    cd ~/code/fslab-handout/ || exit
    make umount || exit
done

 */