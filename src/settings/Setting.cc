// $Id$

#include "Setting.hh"
#include "Observer.hh"
#include "CommandController.hh"
#include "GlobalCommandController.hh"
#include "MSXCommandController.hh"
#include "TclObject.hh"
#include "CliComm.hh"
#include "XMLElement.hh"
#include "checked_cast.hh"
#include <algorithm>
#include <cassert>

using std::string;

namespace openmsx {

Setting::Setting(CommandController& commandController_, const string& name_,
                 const string& description_, SaveSetting save_)
	: commandController(commandController_), name(name_)
	, description(description_), save(save_ == SAVE)
{
}

Setting::~Setting()
{
}

const string& Setting::getName() const
{
	return name;
}

string Setting::getDescription() const
{
	return description;
}

void Setting::changeValueString(const std::string& valueString)
{
	getCommandController().changeSetting(*this, valueString);
}

void Setting::notify() const
{
	Subject<Setting>::notify();
	commandController.getCliComm().update(
		CliComm::SETTING, getName(), getValueString());
}

void Setting::notifyPropertyChange() const
{
	TclObject result;
	info(result);
	commandController.getCliComm().update(
		CliComm::SETTINGINFO, getName(), result.getString());
}

bool Setting::needLoadSave() const
{
	return save;
}

void Setting::setDontSaveValue(const std::string& dontSaveValue_)
{
	dontSaveValue = dontSaveValue_;
}

void Setting::sync(XMLElement& config) const
{
	XMLElement& settings = config.getCreateChild("settings");
	if (!save || hasDefaultValue()) {
		// remove setting
		const XMLElement* elem = settings.findChildWithAttribute(
				"setting", "id", getName());
		if (elem) settings.removeChild(*elem);
	} else {
		// add (or overwrite) setting
		XMLElement& elem = settings.getCreateChildWithAttribute(
				"setting", "id", getName());
		// check for non-saveable value
		// (mechanism can be generalize later when needed)
		string tmp = getValueString();
		if (tmp == dontSaveValue) tmp = getRestoreValueString();
		elem.setData(tmp);
	}
}

void Setting::info(TclObject& result) const
{
	result.addListElement(getTypeString());
	result.addListElement(getDefaultValueString());
	additionalInfo(result);
}

CommandController& Setting::getCommandController() const
{
	return commandController;
}

GlobalCommandController& Setting::getGlobalCommandController() const
{
	GlobalCommandController* globalCommandController =
		dynamic_cast<GlobalCommandController*>(&commandController);
	if (globalCommandController) {
		return *globalCommandController;
	} else {
		return checked_cast<MSXCommandController*>(&commandController)
			->getGlobalCommandController();
	}
}

Interpreter& Setting::getInterpreter() const
{
	return getGlobalCommandController().getInterpreter();
}

} // namespace openmsx
