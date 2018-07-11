/*
 * Rufus: The Reliable USB Formatting Utility
 * Poedit <-> rufus.loc conversion utility
 * Copyright © 2018 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;

[assembly: AssemblyTitle("Pollock")]
[assembly: AssemblyDescription("Poedit ↔ Rufus loc conversion utility")]
[assembly: AssemblyCompany("Akeo Consulting")]
[assembly: AssemblyProduct("Pollock")]
[assembly: AssemblyCopyright("Copyright © 2018 Pete Batard <pete@akeo.ie>")]
[assembly: AssemblyTrademark("GNU GPLv3")]
[assembly: AssemblyVersion("1.0.*")]

namespace pollock
{
    public sealed class Message
    {
        public string id;
        public string str;
        public Message(string id, string str)
        {
            this.id = id;
            this.str = str;
        }
    }

    public sealed class Id
    {
        public string group;
        public string id;
        public Id(string group, string id)
        {
            this.group = group;
            this.id = id;
        }
    }

    public sealed class Language
    {
        public string id;
        public string name;
        public string version;
        public string lcid;
        public SortedDictionary<string, List<Message>> sections;
        public Dictionary<string, string> comments;
        public Language()
        {
            sections = new SortedDictionary<string, List<Message>>();
            comments = new Dictionary<string, string>();
        }
    }

    class Pollock
    {
        private static string app_name = System.Reflection.Assembly.GetExecutingAssembly().GetName().Name;
        private static string app_version = "v"
            + Assembly.GetEntryAssembly().GetName().Version.Major.ToString() + "."
            + Assembly.GetEntryAssembly().GetName().Version.Minor.ToString();
        private static bool cancel_requested = false;
        private const string LANG_ID = "Language";
        private const string LANG_NAME = "X-Rufus-LanguageName";
        private const string LANG_VERSION = "Project-Id-Version";
        private const string LANG_LCID = "X-Rufus-LCID";
        private static Encoding encoding = new UTF8Encoding(false);
        private static List<string> rtl_languages = new List<string> { "ar-SA", "he-IL", "fa-IR" };

        /// <summary>
        /// Wait for a key to be pressed.
        /// </summary>
        static void WaitForKey()
        {
            // Flush the input buffer
            while (Console.KeyAvailable)
                Console.ReadKey(true);
            Console.WriteLine("");
            Console.WriteLine("Press any key to exit...");
            Console.ReadKey(true);
        }

        /// <summary>
        /// Import languages from an existing rufus.loc
        /// </summary>
        /// <param name="path">The directy where the loc file is located.</param>
        /// <returns>A list of Language elements.</returns>
        static List<Language> ParseLocFile(string path)
        {
            var rufus_loc = path + @"\rufus.loc";
            var rufus_pot = path + @"\rufus.pot";
            var watch = System.Diagnostics.Stopwatch.StartNew();
            var lines = File.ReadAllLines(rufus_loc);
            int line_nr = 0;
            string format = "D" + (int)(Math.Log10((double)lines.Count()) + 0.99999);
            string last_key = null;
            string section_name = null;
            string comment = null;
            List<string> parts;
            List<Language> langs = new List<Language>();
            Language lang = null;

            if (!File.Exists(rufus_loc))
            {
                Console.Error.WriteLine($"Could not open {rufus_loc}");
                return null;
            }

            Console.WriteLine($"Importing data from '{rufus_loc}':");

            foreach (var line in lines)
            {
                if (cancel_requested)
                    break;
                ++line_nr;
                Console.SetCursorPosition(0, Console.CursorTop);
                Console.Write($"[{line_nr.ToString(format)}/{lines.Count()}] ");
                var data = line.Trim();
                int i = data.IndexOf("#");
                if (i > 0)
                {
                    comment = data.Substring(i + 1).Trim();
                    data = data.Substring(0, i).Trim();
                }
                if (string.IsNullOrEmpty(data))
                    continue;
                switch (data[0])
                {
                    case '#':
                        comment += data.Substring(1).Trim() + "\n";
                        break;
                    case 'l':
                        comment = null;
                        if (lang != null)
                            langs.Add(lang);
                        lang = new Language();
                        parts = Regex.Matches(data, @"[\""].+?[\""]|[^ ]+")
                            .Cast<Match>()
                            .Select(m => m.Value)
                            .ToList();
                        if (parts.Count < 4)
                        {
                            Console.WriteLine("Error: Invalid 'l' command");
                            return null;
                        }
                        lang.id = parts[1].Replace("\"", "");
                        lang.name = parts[2].Replace("\"", "");
                        Console.WriteLine($"Found language {lang.id} '{lang.name}'");
                        lang.lcid = parts[3];
                        for (i = 4; i < parts.Count; i++)
                            lang.lcid += " " + parts[i];
                        break;
                    case 'a':
                        // This attribue will be restored manually
                        break;
                    case 'g':
                        comment = null;
                        section_name = data.Substring(2).Trim();
                        lang.sections.Add(section_name, new List<Message>());
                        break;
                    case 'v':
                        lang.version = data.Substring(2).Trim();
                        break;
                    case 't':
                        if (data.StartsWith("t MSG") && section_name != "MSG")
                        {
                            section_name = "MSG";
                            lang.sections.Add(section_name, new List<Message>());
                        }
                        if (data[1] != ' ')
                        {
                            Console.WriteLine("Error: Invalid 'l' command");
                            continue;
                        }
                        parts = Regex.Matches(data, @"(?<!\\)"".*?(?<!\\)""|[^ ]+")
                            .Cast<Match>()
                            .Select(m => m.Value)
                            .ToList();
                        if (parts.Count != 3)
                        {
                            Console.WriteLine("Error: Invalid 'l' command");
                            continue;
                        }
                        lang.sections[section_name].Add(new Message(parts[1], parts[2]));
                        last_key = parts[1];
                        if (comment != null)
                        {
                            lang.comments[last_key] = comment.Trim();
                            comment = null;
                        }
                        break;
                    case '"':
                        if (String.IsNullOrEmpty(last_key))
                        {
                            Console.WriteLine($"Error: No previous key for {data}");
                            continue;
                        }
                        lang.sections[section_name].Last().str += data;
                        lang.sections[section_name].Last().str = lang.sections[section_name].Last().str.Replace("\"\"", "");
                        break;
                }
            }
            if (lang != null)
                langs.Add(lang);

            watch.Stop();
            Console.WriteLine($"{(cancel_requested ? "CANCELLED after" : "DONE in")}" +
                $" {watch.ElapsedMilliseconds / 1000.0}s.");

            return langs;
        }

        /// <summary>
        /// Create .po/.pot files from a list of Language elements.
        /// </summary>
        /// <param name="path">The path where the .po/.pot files should be created.</param>
        /// <param name="langs">A lits of Languages elements</param>
        /// <returns>true on success, false on error.</returns>
        static bool CreatePoFiles(string path, List<Language> langs)
        {
            if (langs == null)
                return false;

            var en_US = langs.Find(x => x.id == "en-US");
            if (en_US == null)
                return false;

            foreach (var lang in langs)
            {
                bool is_pot = (lang.id == "en-US");
                var target = path + @"\" + (is_pot ? "rufus.pot" : lang.id + ".po");
                Console.WriteLine($"Creating '{target}'");
                using (var writer = new StreamWriter(target, false, encoding))
                {
                    writer.WriteLine("#, fuzzy");
                    writer.WriteLine();
                    writer.WriteLine("msgid \"\"");
                    writer.WriteLine("msgstr \"\"");
                    writer.WriteLine($"\"Project-Id-Version: {lang.version}\\n\"");
                    writer.WriteLine($"\"Report-Msgid-Bugs-To: pete@akeo.ie\\n\"");
                    writer.WriteLine($"\"POT-Creation-Date: {DateTime.Now.ToString("yyyy-MM-dd HH:mmzz00")}\\n\"");
                    if (is_pot)
                        writer.WriteLine($"\"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\\n\"");
                    else
                        writer.WriteLine($"\"PO-Revision-Date: {DateTime.Now.ToString("yyyy-MM-dd HH:mmzz00")}\\n\"");
                    writer.WriteLine($"\"Last-Translator: FULL NAME <EMAIL@ADDRESS>\\n\"");
                    writer.WriteLine($"\"Language-Team: LANGUAGE <LL@li.org>\\n\"");
                    writer.WriteLine($"\"Language: {lang.id.Replace('-', '_')}\\n\"");
                    writer.WriteLine($"\"MIME-Version: 1.0\\n\"");
                    writer.WriteLine($"\"Content-Type: text/plain; charset=UTF-8\\n\"");
                    writer.WriteLine($"\"Content-Transfer-Encoding: 8bit\\n\"");
                    writer.WriteLine($"\"X-Poedit-SourceCharset: UTF-8\\n\"");
                    writer.WriteLine($"\"X-Rufus-LanguageName: {lang.name}\\n\"");
                    writer.WriteLine($"\"X-Rufus-LCID: {lang.lcid}\\n\"");

                    foreach (var section in lang.sections)
                    {
                        foreach (var msg in section.Value)
                        {
                            writer.WriteLine();
                            if (section.Key == "MSG")
                                writer.WriteLine($"#. • {msg.id}");
                            else
                                writer.WriteLine($"#. • {section.Key} → {msg.id}");
                            if (lang.comments.ContainsKey(msg.id))
                            {
                                if (is_pot)
                                    writer.WriteLine("#.");
                                foreach (var comment in lang.comments[msg.id].Split('\n'))
                                    if (comment.Trim() != "")
                                        writer.WriteLine((is_pot ? "#. " : "# ") + comment);
                            }
                            if (is_pot)
                            {
                                writer.WriteLine($"msgid {msg.str}");
                                writer.WriteLine("msgstr \"\"");
                            }
                            else
                            {
                                writer.WriteLine($"msgid {en_US.sections[section.Key].Find(x => x.id == msg.id).str}");
                                writer.WriteLine($"msgstr {msg.str}");
                            }
                        }
                    }
                }
            }
            Console.WriteLine("DONE.");
            return true;
        }

        /// <summary>
        /// Create a Language entry from a .po or .pot file.
        /// </summary>
        /// <param name="file">The name of the .po/.pot file.</param>
        /// <returns>A Language element or null on error.</returns>
        static Language ParsePoFile(string file)
        {
            if (!File.Exists(file))
            {
                Console.Error.WriteLine($"Could not open {file}");
                return null;
            }
            Console.WriteLine($"Importing data from '{file}':");
            bool is_pot = file.EndsWith(".pot");
            var watch = System.Diagnostics.Stopwatch.StartNew();
            var lines = File.ReadAllLines(file);
            string format = "D" + (int)(Math.Log10((double)lines.Count()) + 0.99999);
            int line_nr = 0;
            // msg_data[0] -> msgid, msg_data[1] -> msgstr
            string[] msg_data = new string[2] { null, null };
            Language lang = new Language();
            List<Id> ids = new List<Id>();
            List<string> comments = new List<string>();
            List<string> codes = new List<string>();
            int msg_type = 0;
            foreach (var line in lines)
            {
                if (cancel_requested)
                    break;
                ++line_nr;
                Console.SetCursorPosition(0, Console.CursorTop);
                Console.Write($"[{line_nr.ToString(format)}/{lines.Count()}] ");
                var data = line.Trim();
                if (!data.StartsWith("\""))
                {
                    var options = new Dictionary<string, string>();
                    if ((msg_type == 1) && (string.IsNullOrEmpty(msg_data[0])) && (!string.IsNullOrEmpty(msg_data[1])))
                    {
                        // Process the header
                        string[] header = msg_data[1].Split(new string[] { "\\n" }, StringSplitOptions.None);
                        foreach (string header_line in header)
                        {
                            if (string.IsNullOrEmpty(header_line))
                                continue;
                            string[] opt = header_line.Split(new string[] { ": " }, StringSplitOptions.None);
                            if (opt.Length != 2)
                            {
                                Console.WriteLine($"ERROR: Invalid header line '{header_line}'");
                                continue;
                            }
                            options.Add(opt[0], opt[1]);
                        }
                        lang.id = options[LANG_ID].Replace('_', '-');
                        lang.name = options[LANG_NAME];
                        lang.version = options[LANG_VERSION];
                        lang.lcid = options[LANG_LCID];
                    }
                }
                if (data.StartsWith("\""))
                {
                    if (data[data.Length - 1] != '"')
                    {
                        Console.WriteLine("ERROR: Unexpected quoted data");
                        continue;
                    }
                    msg_data[msg_type] += data.Substring(1, data.Length - 2);
                }
                else if (data.StartsWith("msgid "))
                {
                    if (data[6] != '"')
                    {
                        Console.WriteLine("ERROR: Unexpected data after 'msgid'");
                        continue;
                    }
                    msg_type = 0;
                    msg_data[msg_type] = data.Substring(7, data.Length - 8);
                }
                else if (data.StartsWith("msgstr "))
                {
                    if (data[7] != '"')
                    {
                        Console.WriteLine("ERROR: Unexpected data after 'msgstr'");
                        continue;
                    }
                    msg_type = 1;
                    msg_data[msg_type] = data.Substring(8, data.Length - 9);
                }
                else if (data.StartsWith("#. •"))
                {
                    if (data.StartsWith("#. • MSG"))
                    {
                        ids.Add(new Id("MSG", data.Substring(5).Trim()));
                    }
                    else
                    {
                        string[] str = data.Substring(5).Split(new string[] { " → " }, StringSplitOptions.None);
                        if (str.Length != 2)
                            Console.WriteLine($"ERROR: Invalid ID {data}");
                        else
                            ids.Add(new Id(str[0].Trim(), str[1].Trim()));
                    }
                }
                else if (data.StartsWith("#. "))
                {
                    if (comments == null)
                        comments = new List<string>();
                    comments.Add(data.Substring(2).Trim());
                }
                // Break or EOF => Process the previous section
                if (string.IsNullOrEmpty(data) || (line_nr == lines.Count()))
                {
                    if ((!string.IsNullOrEmpty(msg_data[0])) && (ids.Count() != 0))
                    {
                        foreach (var id in ids)
                        {
                            if (comments != null)
                            {
                                lang.comments.Add(id.id, "");
                                foreach (var comment in comments)
                                    lang.comments[id.id] += comment + "\n";
                            }
                            // Ignore messages that have the same translation as en-US
                            if (msg_data[0] == msg_data[1])
                                continue;
                            if (!lang.sections.ContainsKey(id.group))
                                lang.sections.Add(id.group, new List<Message>());
                            lang.sections[id.group].Add(new Message(id.id, msg_data[is_pot ? 0 : 1]));
                        }
                    }
                    ids = new List<Id>();
                    comments = null;
                }
            }

            // Sort the MSG section alphabetically
            lang.sections["MSG"] = lang.sections["MSG"].OrderBy(x => x.id).ToList();

            watch.Stop();
            Console.WriteLine($"{(cancel_requested ? "CANCELLED after" : "DONE in")}" +
                $" {watch.ElapsedMilliseconds / 1000.0}s.");

            return lang;
        }

        /// <summary>
        /// Write a loc language section.
        /// </summary>
        /// <param name="writer">A streamwriter to the file to write to.</param>
        /// <param name="lang">The Language to write.</param>
        static void WriteLoc(StreamWriter writer, Language lang)
        {
            bool is_pot = (lang.id == "en-US");
            bool is_rtl = rtl_languages.Contains(lang.id);
            writer.WriteLine($"l \"{lang.id}\" \"{lang.name}\" {lang.lcid}");
            writer.WriteLine($"v {lang.version}");
            if (!is_pot)
                writer.WriteLine("b \"en-US\"");
            if (is_rtl)
                writer.WriteLine("a \"r\"");

            var sections = lang.sections.Keys.ToList();
            foreach (var section in sections)
            {
                writer.WriteLine();
                if (section != "MSG")
                    writer.WriteLine($"g {section}");
                foreach (var msg in lang.sections[section])
                {
                    if (lang.comments.ContainsKey(msg.id))
                    {
                        foreach (var l in lang.comments[msg.id].Split('\n'))
                            if (l.Trim() != "")
                                writer.WriteLine($"# {l}");
                    }
                    writer.WriteLine($"t {msg.id} \"{msg.str}\"");
                }
            }
        }

        /// <summary>
        /// Create a new rufus.loc from a list of Language elements.
        /// </summary>
        /// <param name="path">The path where the new 'rufus.loc' should be created.</param>
        /// <param name="list">The list of Language elements.</param>
        /// <returns>true on success, false on error.</returns>
        static bool UpdateLocFile(string path, Language lang)
        {
            if (lang == null)
                return false;
            Encoding encoding = new UTF8Encoding(false);
            var watch = System.Diagnostics.Stopwatch.StartNew();
            var target = path + @"\rufus.loc";
            var lines = File.ReadAllLines(target);
            using (var writer = new StreamWriter(target, false, encoding))
            {
                bool skip = false;
                foreach (var line in lines)
                {
                    if (line.StartsWith($"l \"{lang.id}\""))
                    {
                        skip = true;
                        WriteLoc(writer, lang);
                        writer.WriteLine();
                    }
                    else if (line.StartsWith("######"))
                    {
                        skip = false;
                    }
                    if (!skip)
                        writer.WriteLine(line);
                }
            }
            return true;
        }

        /// <summary>
        /// Create a new rufus.loc from a list of Language elements.
        /// </summary>
        /// <param name="path">The path where the new 'rufus.loc' should be created.</param>
        /// <param name="list">The list of Language elements.</param>
        /// <returns>true on success, false on error.</returns>
        static bool SaveLocFile(string path, List<Language> list)
        {
            if ((list == null) || (list.Count == 0))
                return false;

            var watch = System.Diagnostics.Stopwatch.StartNew();
            var target = path + @"\rufus.loc";
            Console.WriteLine($"Creating '{target}':");
            using (var writer = new StreamWriter(target, false, encoding))
            {
                var notice = $"### Autogenerated by {app_name} {app_version} for use with Rufus - DO NOT EDIT!!! ###";
                var sep = new String('#', notice.Length);
                writer.WriteLine(sep);
                writer.WriteLine(notice);
                writer.WriteLine(sep);
                writer.WriteLine();
                writer.WriteLine("# List of all languages included in this file (with version)");
                foreach (var lang in list)
                {
                    writer.WriteLine($"# • v{lang.version} \"{lang.id}\" \"{lang.name}\"");
                }
                foreach (var lang in list)
                {
                    if (cancel_requested)
                        break;
                    Console.WriteLine($"Adding {lang.id}");
                    writer.WriteLine();
                    writer.WriteLine(sep);
                    WriteLoc(writer, lang);
                }
            }
            watch.Stop();
            Console.WriteLine($"{(cancel_requested ? "CANCELLED after" : "DONE in")}" +
                $" {watch.ElapsedMilliseconds / 1000.0}s.");
            return true;
        }

        static void Main(string[] args)
        {
            Console.OutputEncoding = System.Text.Encoding.UTF8;
            Console.CancelKeyPress += delegate (object sender, ConsoleCancelEventArgs e) {
                e.Cancel = true;
                cancel_requested = true;
            };

            Console.WriteLine($"{app_name} {app_version} - Poedit to rufus.loc conversion utility");

            var path = @"C:\pollock";

            // NB: Can find PoEdit from Computer\HKEY_CURRENT_USER\Software\Classes\Local Settings\Software\Microsoft\Windows\Shell\MuiCache

            //CreatePoFiles(path, ParseLocFile(@"C:\rufus\res\localization"));

            var en_US = ParsePoFile(path + @"\rufus.pot");
            var fr_FR = ParsePoFile(path + @"\fr-FR.po");
            var ar_SA = ParsePoFile(path + @"\ar-SA.po");
            var vi_VN = ParsePoFile(path + @"\vi-VN.po");
            List<Language> list = new List<Language>();
            list.Add(en_US);
            list.Add(ar_SA);
            list.Add(fr_FR);
            list.Add(vi_VN);
            SaveLocFile(path, list);
            //            UpdateLocFile(path + @"\test", fr_FR);

            WaitForKey();
        }
    }
}
