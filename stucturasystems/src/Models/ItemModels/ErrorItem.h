//
// Created by Moritz Herzog on 23.04.25.
//

#ifndef STRUCTURASYSTEMS_ERRORITEM_H
#define STRUCTURASYSTEMS_ERRORITEM_H

#include <boost/uuid/uuid.hpp>
#include <string>

namespace StructuraSystems::Client {
    enum ErrorType {
        ERROR,
        WARNING
    };

    class ErrorItem {
    public:
        ErrorItem(boost::uuids::uuid elementId, std::string projectName, ErrorType type, std::string description);
        ~ErrorItem() = default;

        boost::uuids::uuid getElementID();
        std::string getProjectName();
        ErrorType errorType();
        std::string description();

    private:
        boost::uuids::uuid ElementId;
        std::string ProjectName;
        ErrorType Type;
        std::string Description;
    };
} // StructuraSystems::Client

#endif //STRUCTURASYSTEMS_ERRORITEM_H
