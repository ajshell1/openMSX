# $Id$

include build/node-start.mk

SRC_HDR:= \
	CommandException \
	CommandController \
	Completer \
	Command \
	InfoCommand \
	InfoTopic \
	Interpreter \
	TclObject \

HDR_ONLY:= \
	InterpreterOutput \
	CommandRegistry

include build/node-end.mk

