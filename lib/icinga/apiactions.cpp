/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2015 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "icinga/apiactions.hpp"
#include "icinga/service.hpp"
#include "icinga/pluginutility.hpp"
#include "remote/apiaction.hpp"
#include "remote/httputility.hpp"
#include "base/utility.hpp"
#include "base/convert.hpp"

using namespace icinga;

REGISTER_APIACTION(reschedule_check, "Service;Host", &ApiActions::RescheduleCheck);
REGISTER_APIACTION(process_check_result, "Service;Host", &ApiActions::ProcessCheckResult);
/*REGISTER_APIACTION(enable_svc_checks, "Hostgroup;ServiceGroup", &ApiActions::EnableSvcChecks);
REGISTER_APIACTION(disable_svc_checks, "Hostgroup;ServiceGroup", &ApiActions::DisableSvcChecks);*/
REGISTER_APIACTION(enable_passive_checks, "Service;Host;ServiceGroup;HostGroup", &ApiActions::EnablePassiveChecks);
REGISTER_APIACTION(disable_passive_checks, "Service;Host;ServiceGroup;HostGroup", &ApiActions::DisablePassiveChecks);
REGISTER_APIACTION(acknowledge_problem, "Service;Host", &ApiActions::AcknowledgeProblem);

Dictionary::Ptr ApiActions::CreateResult(int code, const String& status)
{
	Dictionary::Ptr result = new Dictionary();
	result->Set("code", code);
	result->Set("status", status);
	return result;
}

Dictionary::Ptr ApiActions::RescheduleCheck(const DynamicObject::Ptr& object, const Dictionary::Ptr& params)
{
	Checkable::Ptr checkable = static_pointer_cast<Checkable>(object);

	if (!checkable)
		return ApiActions::CreateResult(404, "Cannot reschedule check for non-existent object");

	if (Convert::ToBool(HttpUtility::GetLastParameter(params, "force")))
		checkable->SetForceNextCheck(true);

	double nextCheck;
	if (params->Contains("next_check"))
		nextCheck = HttpUtility::GetLastParameter(params, "next_check");
	else
		nextCheck = Utility::GetTime();

	checkable->SetNextCheck(nextCheck);

	return ApiActions::CreateResult(200, "Successfully rescheduled check for " + checkable->GetName());
}

Dictionary::Ptr ApiActions::ProcessCheckResult(const DynamicObject::Ptr& object, const Dictionary::Ptr& params)
{
	Checkable::Ptr checkable = static_pointer_cast<Checkable>(object);

	if (!checkable)
		return ApiActions::CreateResult(404, "Cannot process passive check result for non-existent object");

	if (!checkable->GetEnablePassiveChecks())
		return ApiActions::CreateResult(403, "Passive checks are disabled for " + checkable->GetName());

	String name;

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	if (!params->Contains("exit_status"))
		return ApiActions::CreateResult(403, "Parameter 'exit_status' is required");

	int exitStatus = HttpUtility::GetLastParameter(params, "exit_status");

	ServiceState state;

	if (!service) {
		name = host->GetName();
		if (exitStatus == 0)
			state = ServiceOK;
		else if (exitStatus == 1)
			state = ServiceCritical;
		else
			return ApiActions::CreateResult(403, "Invalid 'exit_status' for Host " + name);
	} else {
		state = PluginUtility::ExitStatusToState(exitStatus);
		name = service->GetName() + "!" + service->GetHostName();
	}

	if (!params->Contains("output"))
		return ApiActions::CreateResult(403, "Parameter 'output' is required");

	CheckResult::Ptr cr = new CheckResult();
	cr->SetOutput(HttpUtility::GetLastParameter(params, "output"));
	cr->SetState(state);

	cr->SetCheckSource(HttpUtility::GetLastParameter(params, "check_source"));
	cr->SetPerformanceData(params->Get("performance_data"));
	cr->SetCommand(params->Get("command"));
	cr->SetExecutionEnd(HttpUtility::GetLastParameter(params, "execution_end"));
	cr->SetExecutionStart(HttpUtility::GetLastParameter(params, "execution_start"));
	cr->SetScheduleEnd(HttpUtility::GetLastParameter(params, "schedule_end"));
	cr->SetScheduleStart(HttpUtility::GetLastParameter(params, "schedule_start"));

	checkable->ProcessCheckResult(cr);

	/* Reschedule the next check. The side effect of this is that for as long
	 * as we receive passive results for a service we won't execute any
	 * active checks. */
	checkable->SetNextCheck(Utility::GetTime() + checkable->GetCheckInterval());

	return ApiActions::CreateResult(200, "Successfully processed check result for " + name);
}
/*
Dictionary::Ptr ApiActions::EnableSvcChecks(const DynamicObject::Ptr& object, const Dictionary::Ptr& params)
{
	Checkable::Ptr checkable = static_pointer_cast<Checkable>(object);

	if (!checkable)
		return ApiActions::CreateResult(404, "Cannot enable checks for non-existent object");

	//checkable == HostGroup or ServiceGroup?

}
*/
Dictionary::Ptr ApiActions::EnablePassiveChecks(const DynamicObject::Ptr& object, const Dictionary::Ptr& params)
{
	//TODO check if group undso
	Checkable::Ptr checkable = static_pointer_cast<Checkable>(object);

	if (!checkable)
		return ApiActions::CreateResult(404, "Cannot enable passive checks for non-existent object");

	checkable->SetEnablePassiveChecks(true);

	return ApiActions::CreateResult(200, "Successfully enabled passive checks for " + checkable->GetName());
}

Dictionary::Ptr ApiActions::DisablePassiveChecks(const DynamicObject::Ptr& object, const Dictionary::Ptr& params)
{
	//TODO check if group undso
	Checkable::Ptr checkable = static_pointer_cast<Checkable>(object);

	if (!checkable)
		return ApiActions::CreateResult(404, "Cannot disable passive checks non-existent object");

	checkable->SetEnablePassiveChecks(false);

	return ApiActions::CreateResult(200, "Successfully disabled passive checks for " + checkable->GetName());
}

Dictionary::Ptr ApiActions::AcknowledgeProblem(const DynamicObject::Ptr& object, const Dictionary::Ptr& params)
{
	Checkable::Ptr checkable = static_pointer_cast<Checkable>(object);

	if (!checkable)
		return ApiActions::CreateResult(404, "Cannot acknowledge propblem for non-existent object");

	if (!params->Contains("author") || !params->Contains("comment"))
		return ApiActions::CreateResult(403, "Acknowledgements require an author and a comment");
	
	AcknowledgementType sticky = AcknowledgementNormal;
	bool notify = false;
	double timestamp = 0;
	if (params->Contains("sticky"))
		sticky = AcknowledgementSticky;
	if (params->Contains("notify"))
		notify = true;
	if (params->Contains("timestamp"))
		timestamp = params->Get("timestamp");

	Host::Ptr host;
	Service::Ptr service;
	tie(host, service) = GetHostService(checkable);

	if (!service) {
		if (host->GetState() == HostUp)
			return ApiActions::CreateResult(409, "Host " + checkable->GetName() + " is up");
	} else {
		if (service->GetState() == ServiceOK)
			return ApiActions::CreateResult(409, "Service " + checkable->GetName() + " is ok");
	}

	checkable->AddComment(CommentAcknowledgement, params->Get("author"), params->Get("comment"), timestamp);
	checkable->AcknowledgeProblem(params->Get("author"), params->Get("comment"), sticky, notify, timestamp);
	return ApiActions::CreateResult(200, "Successfully acknowledged problem for " +  checkable->GetName());
}
