// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2019 The Safe Core developers
#include "governance-types.h"

GovernanceObjectType GovernanceStringToType(std::string strType) {
    GovernanceObjectType type = Error;

    if(strType == "AllTypes") type = AllTypes;
    if(strType == "Error") type = Error;
    if(strType == "ValueOverride") type = ValueOverride;
    if(strType == "SafeNetwork") type = SafeNetwork;
    if(strType == "SafeNetworkVariable") type = SafeNetworkVariable;
    if(strType == "Category") type = Category;
    if(strType == "Group") type = Group;
    if(strType == "User") type = User;
    if(strType == "Company") type = Company;
    if(strType == "Project") type = Project;
    if(strType == "ProjectReport") type = ProjectReport;
    if(strType == "ProjectMilestone") type = ProjectMilestone;
    if(strType == "Proposal") type = Proposal;
    if(strType == "Contract") type = Contract;

    return type;
};

std::string GovernanceTypeToString(GovernanceObjectType type) {
    std::string s = "";

    if(type == AllTypes) s = "AllTypes";
    if(type == Error) s = "Error";
    if(type == ValueOverride) s = "ValueOverride";
    if(type == SafeNetwork) s = "SafeNetwork";
    if(type == SafeNetworkVariable) s = "SafeNetworkVariable";
    if(type == Category) s = "Category";
    if(type == Group) s = "Group";
    if(type == User) s = "User";
    if(type == Company) s = "Company";
    if(type == Project) s = "Project";
    if(type == ProjectReport) s = "ProjectReport";
    if(type == ProjectMilestone) s = "ProjectMilestone";
    if(type == Proposal) s = "Proposal";
    if(type == Contract) s = "Contract";

    return s;
};