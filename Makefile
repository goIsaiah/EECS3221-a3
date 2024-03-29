production:
	cc New_Alarm_Cond.c Command_Parser.c -pthread

debug:
	cc New_Alarm_Cond.c Command_Parser.c -DDEBUG -g -pthread

