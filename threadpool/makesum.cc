#include<iostream>
#include<string>
#include<stdlib.h>
using namespace std;

int main()
{
    char content[8192];
    string args=getenv("QUERY_STRING");
    int pos;
    if((pos=args.find("&"))==string::npos)
    {
        cout<<"must be tow args"<<endl;
        return 1;
    }
    string s1=args.substr(0,pos);
    string s2=args.substr(pos+1,args.size()-pos-1);
    int sum=stoi(s1)+stoi(s2);
    sprintf(content,"the sum is %d \r\n",sum);

    printf("Content_length: %d\r\n\r\n",(int)sizeof(content));
    printf("%s",content);
    fflush(stdout);

    exit(0);
}