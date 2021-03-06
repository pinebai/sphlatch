#include <string>
#include <boost/lexical_cast.hpp>
#include "typedefs.h"

typedef sphlatch::vect3dT   vect3dT;
typedef sphlatch::fType     fType;

vect3dT vectOptParse(std::string str)
{
   vect3dT v;

   if (str.size() == 0)
   {
      std::cerr << "Use proper vector format \"[x,y,z]\" !\n";
      exit(EXIT_FAILURE);
   }

   if (str.substr(0, 1) == "=")
      str.erase(0, 1);

   while (str.find(" ") != str.npos)
      str.erase(str.find(" "), 1);

   if (str.substr(0, 1) == "[")
   {
      str.erase(0, 1);
      v[0] = boost::lexical_cast<fType>(
         str.substr(0, str.find(",")));
      str.erase(0, str.find(",") + 1);
   }
   else
   {
      std::cerr << "Use proper vector format \"[x,y,z]\" !\n";
      exit(EXIT_FAILURE);
   }

   v[1] = boost::lexical_cast<fType>(str.substr(0, str.find(",")));
   str.erase(0, str.find(",") + 1);

   v[2] = boost::lexical_cast<fType>(str.substr(0, str.find("]")));
   str.erase(0, str.find("]") + 1);

   return(v);
}

// The last extra parser never seems to work, so insert a dummy:

std::pair<std::string, std::string> dummyOptParser(const std::string& str)
{
   return(std::make_pair(std::string(), std::string()));
}
