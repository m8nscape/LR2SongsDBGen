#include "cfg.h"
#include "utils.h"
#include <tinyxml2.h>
using namespace tinyxml2;

std::vector<std::string> getJukebox(const std::filesystem::path& xml)
{
    std::vector<std::string> result;

    XMLError err;

    XMLDocument doc;
    err = doc.LoadFile(xml.string().c_str());
    if (err != XML_SUCCESS)
    {
        std::cout << "XML LoadFile failed: " << err << std::endl;
        return result;
    }

    try
    {
        auto p1 = doc.FirstChildElement("config");
        if (p1 == NULL)
        {
            std::cout << "XML node 'config' not found" << std::endl;
            return result;
        }
        auto p2 = p1->FirstChildElement("jukebox");
        if (p2 == NULL)
        {
            std::cout << "XML node 'jukebox' not found" << std::endl;
            return result;
        }
        auto p3 = p2->FirstChildElement("path");
        while (p3 != NULL)
        {
            auto text = p3->GetText();
            if (text != NULL)
            {
                result.push_back(std::string(text));
            }
            p3 = p3->NextSiblingElement("path");
        }
    }
    catch (...)
    {
        std::cout << "XML Read failed" << std::endl;
        return {};
    }

    return result;
}