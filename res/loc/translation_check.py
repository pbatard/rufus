#! /usr/bin/env python3
# -*- coding=utf-8 -*-

# LIMITATIONS
# this script was written as fast as possible so it is broken/minimalistic:
# it assumes the first language in rufus.loc in English
# written for 1.0.23, things may have changes since then
# horrible code below
# in multiline messages checks only first one if is the same as in English (should be enough for now)

import sys
# regex
import re
# splitting by "quotes"
import shlex
# pretty print for debug
import pprint

# global variables, I'm no python expert so I'll just dump everything here
languages = []
unavailable = []
untranslated = []
version = "0.0.0"
ignored_messages = ['IDOK', 'MSG_020', 'MSG_021', 'MSG_022', 'MSG_023', 'MSG_024', 'MSG_025', 'MSG_026', 'MSG_027', 'MSG_028', 'MSG_118'] 

class Language:
    code = "xx-XX"
    name = "None"
    version = "0.0.0"
    base = "en-US"
    groups = {}
    #{ "group" => { "MNEMONIC => "transl" }, group2 = (...) }
    def __init__(self):
        self.groups = {}

    def get_current_group(self):
        if len(self.groups) > 0:
            return list(self.groups.keys())[len(self.groups)-1]
        else:
            return "none"
    def get_current_message(self):
        current_group = self.get_current_group()
        if len(self.groups[current_group]) > 0:
            return list(self.groups[current_group].keys())[len(self.groups[current_group])-1]
        else:
            return "none"


def print_no_new(data):
    print(data, end="")

def load_languages():
    print_no_new("loading rufus.loc file...\t")
    sys.stdout.flush()
    #load file
    lines = [line.strip() for line in open("rufus.loc", "r", encoding="utf8")]

    # remove comments
    #TODO properly remove comments, now it'll break messages with "#" in them, I just assumes there aren't any
    #lines = [re.sub(re.compile("#(.*)?" ) ,"" ,line) for line in lines]
    # remove empty lines
    #lines = filter(None, lines)

    i = -1
    exceptions = []
    for index, line in enumerate(lines):
        try:
            if line != "" and line[0] != "#":
                if line[0] == "l":
                    #this is language declaration
                    i += 1
                    temp_language = Language()
                    temp_language.name = shlex.split(line)[2]
                    temp_language.code = shlex.split(line)[1]
                    languages.append(temp_language)
                    # print("current language: "+languages[i].name)

                elif line[0] == "v":
                    #this is language declaration
                    version = line.split(" ")[1]
                    languages[i].version = line.split(" ")[1]
                elif line[0] == "b":
                    languages[i].base = shlex.split(line)[1]
                    if line != "b \"en-US\"":
                        print(languages[i].name + " is based on another translation: " + languages[i].base)
                elif line[0] == "g":
                    languages[i].groups[shlex.split(line)[1]] = {}
                    # print("current group: "+languages[i].get_current_group())
                elif line[0] == "t":
                    pass
                    try:
                        temp_translation = shlex.split(line) #errors
                    except ValueError as err:
                        exceptions.append(str(err)+" in line "+ str(index+1)+": "+line)
                    languages[i].groups[languages[i].get_current_group()][temp_translation[1]] = temp_translation[2]
                    #print("cur transl: "+languages[i].get_current_message())
                elif line[0] == "\"":
                    pass
                elif line[0] == "a":
                    pass #only RTL indication as for now
                else:
                    raise Exception("unknown line "+str(index+1)+ ": "+line)
        except Exception as error:
            exceptions.append("Error: "+ repr(error))


    if len(exceptions) > 0:
        print("[WARNING]")
        print("\n".join(exceptions))
    else:
        print("[OK]")
    print()
    print ("Found " + str(len(languages)) + " languages")
    version = languages[0].version
    print("Assuming " + version + " is the latest version. Only languages with this version will be checked...")
    #comment if you want to check every single language, not only newest versions
    languages[:] = [language for language in languages if language.version==version]
    print (str(len(languages)) + " languages in " + languages[0].version + " version:")
    print ("\n".join(language.name for language in languages))
    print()

def check_unavailable():
    print_no_new("Checking for possible missing messages...\t")
    sys.stdout.flush()
    #for groups in english
        #every lang
            #does group exist
            #for strings in groups
                #does it exist
    for group in languages[0].groups:
        for language in languages[1::]:
            if group not in language.groups:
                unavailable.append("Language " + language.name + " is missing group '" + group + "'")
            else:
                for string in languages[0].groups[group]:
                    if string not in language.groups[group] and string not in ignored_messages:
                        unavailable.append("Language " + language.name + " is missing message '" + string + "'")
    # print output
    if len(unavailable) > 0:
        print("[WARNING]")
        print("Possible missing messages:")
        print("\n".join(unavailable))
    else:
        print("[OK]")
    print()

def check_untranslated():
    print_no_new("Checking for possible untranslated messages...\t")
    sys.stdout.flush()
    #evyerything but english
        #for group
            #for string
                #transl==english?
    for language in languages[1::]:
        for group in language.groups:
            for string in language.groups[group]:
                try:
                    if language.groups[group][string] == languages[0].groups[group][string] and string not in ignored_messages:
                        untranslated.append("Language " + language.name + ": message '" + string + "' has the same translation as English")
                except KeyError as error:
                    untranslated.append("Language " + language.name + ": message '" + string + "' doesn't exist in English")
    # print output
    if len(untranslated) > 0:
        print("[WARNING]")
        print("Possible untranslated messages:")
        print("\n".join(untranslated))
    else:
        print("[OK]")
    print()

def main():
    print("Rufus translation helper tool, proof of concept/MVP")
    load_languages()
    #pp = pprint.PrettyPrinter(indent=4)
    #pp.pprint(languages[2].groups)
    check_unavailable()
    check_untranslated()

#load main only in ran as stand-alone script
if __name__ == "__main__":
    main()
