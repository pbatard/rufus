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

/*
 * Icon courtesy of Axialis Fluent Pro 2018 - Letters and Symbols.
 * CC BY-ND 4.0 - https://www.axialis.com/icons.
 */

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Cache;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

[assembly: AssemblyTitle("Pollock")]
[assembly: AssemblyDescription("Poedit ↔ Rufus loc conversion utility")]
[assembly: AssemblyCompany("Akeo Consulting")]
[assembly: AssemblyProduct("Pollock")]
[assembly: AssemblyCopyright("Copyright © 2018 Pete Batard <pete@akeo.ie>")]
[assembly: AssemblyTrademark("GNU GPLv3")]
[assembly: AssemblyVersion("1.1.*")]

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

        public override bool Equals(object obj)
        {
            Id o = obj as Id;

            return (o.group == this.group) && (o.id == this.id);
        }

        public override int GetHashCode()
        {
            return (this.group + ":" + this.id).GetHashCode();
        }

        public override string ToString()
        {
            if (this.group == "MSG")
                return this.id;
            return this.group + " → " + this.id;
        }
    }

    public sealed class Language
    {
        public string id;
        public string name;
        public string version;
        public string lcid;
        public SortedDictionary<string, List<Message>> sections;
        public Dictionary<Id, string> comments;
        public Dictionary<Id, string> id_to_str;
        public Language()
        {
            sections = new SortedDictionary<string, List<Message>>();
            comments = new Dictionary<Id, string>();
            id_to_str = new Dictionary<Id, string>();
        }
    }

    class Pollock
    {
        private static string app_name = CultureInfo.CurrentCulture.TextInfo.ToTitleCase(Assembly.GetExecutingAssembly().GetName().Name);
        private static string app_dir = AppDomain.CurrentDomain.BaseDirectory;
        private static int[] version = new int[2]
            { Assembly.GetEntryAssembly().GetName().Version.Major, Assembly.GetEntryAssembly().GetName().Version.Minor };
        private static string version_str = "v" + version[0].ToString() + "." + version[1].ToString();
        private static bool cancel_requested = false;
        private const string LANG_ID = "Language";
        private const string LANG_NAME = "X-Rufus-LanguageName";
        private const string LANG_VERSION = "Project-Id-Version";
        private const string LANG_LCID = "X-Rufus-LCID";
        private static Encoding encoding = new UTF8Encoding(false);
        private static List<string> rtl_languages = new List<string> { "ar-SA", "he-IL", "fa-IR" };
        private static Stopwatch sw = new System.Diagnostics.Stopwatch();
        private static DateTime last_changed = DateTime.MinValue;
        private static int download_status;
        private static int console_x_pos;
        private static bool in_progress = false;
        private static bool in_on_change = false;
        private static double speed = 0.0f;

        /// <summary>
        /// Wait for a key to be pressed.
        /// </summary>
        static void WaitForKey(string msg = null)
        {
            if (msg == null)
                msg = "Press any key to continue...";
            // Flush the input buffer
            while (Console.KeyAvailable)
                Console.ReadKey(true);
            Console.WriteLine("");
            Console.WriteLine(msg);
            while (!Console.KeyAvailable)
                Thread.Sleep(50);
            Console.ReadKey(true);
            while (Console.KeyAvailable)
                Console.ReadKey(true);
        }

        /// <summary>
        /// Import languages from an existing Rufus loc file
        /// </summary>
        /// <param name="file">The name of the loc file.</param>
        /// <param name="select_id">(Optional) If specified, only the language with this id, along with en-US will be returned.</param>
        /// <returns>A list of Language elements.</returns>
        static List<Language> ParseLocFile(string file, string select_id = null)
        {
            var lines = File.ReadAllLines(file);
            int line_nr = 0;
            string format = "D" + (int)(Math.Log10((double)lines.Count()) + 0.99999);
            string last_key = null;
            string section_name = null;
            string comment = null;
            List<string> parts;
            List<Language> langs = new List<Language>();
            Language lang = null;
            bool skip_line = false;
            bool found_my_id = false;
            Id id;

            if (!File.Exists(file))
            {
                Console.Error.WriteLine($"Could not open {file}");
                return null;
            }

            Console.Write($"Importing data from '{file}'... ");

            foreach (var line in lines)
            {
                if ((cancel_requested) || (found_my_id && skip_line))
                    break;
                ++line_nr;
                //Console.SetCursorPosition(0, Console.CursorTop);
                //Console.Write($"[{line_nr.ToString(format)}/{lines.Count()}] ");
                var data = line.Trim();
                int i = data.IndexOf("#");
                if (i > 0)
                {
                    comment = data.Substring(i + 1).Trim();
                    data = data.Substring(0, i).Trim();
                }
                if (string.IsNullOrEmpty(data))
                    continue;
                if (skip_line && data[0] != 'l')
                    continue;
                switch (data[0])
                {
                    case '#':
                        comment += data.Substring(1).Trim() + "\n";
                        break;
                    case 'l':
                        comment = null;
                        parts = Regex.Matches(data, @"[\""].+?[\""]|[^ ]+")
                            .Cast<Match>()
                            .Select(m => m.Value)
                            .ToList();
                        if (parts.Count < 4)
                        {
                            Console.WriteLine("Error: Invalid 'l' command");
                            return null;
                        }
                        string cur_id = parts[1].Replace("\"", "");
                        if (select_id != null)
                        {
                            if ((select_id == "en-US") && (cur_id != "en-US"))
                                skip_line = true;
                            else if ((!skip_line) && (select_id != cur_id) && (cur_id != "en-US"))
                                skip_line = true;
                            else if (skip_line && (select_id == cur_id))
                                skip_line = false;
                            if (select_id == cur_id)
                                found_my_id = true;
                            if (skip_line)
                                break;
                        }
                        if (lang != null)
                            langs.Add(lang);
                        lang = new Language();
                        lang.id = parts[1].Replace("\"", "");
                        lang.name = parts[2].Replace("\"", "");
                        //Console.WriteLine($"Found language {lang.id} '{lang.name}'");
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
                        // We also maintain global list of Id -> str for convenience
                        lang.id_to_str.Add(new Id(section_name, (parts[1])), parts[2]);
                        last_key = parts[1];
                        if (comment != null)
                        {
                            id = new Id(section_name, last_key);
                            lang.comments[id] = comment.Trim();
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
                        id = new Id(section_name, last_key);
                        lang.id_to_str[id] += data;
                        lang.id_to_str[id] = lang.id_to_str[id].Replace("\"\"", "");
                        break;
                }
            }
            if (lang != null)
                langs.Add(lang);

            Console.WriteLine(cancel_requested ? "CANCELLED" : "DONE");

            return langs;
        }

        /// <summary>
        /// Create .po/.pot files from a list of Language elements.
        /// </summary>
        /// <param name="langs">A lits of Language objects to process.</param>
        /// <param name="old_en_US">(Optional) A previous version of en-US to use for comparison.</param>
        /// <param name="path">(Optional) The path where the .po/.pot files should be created.</param>
        /// <returns>The number of PO files created.</returns>
        static int CreatePoFiles(List<Language> langs, Language old_en_US = null, string path = null)
        {
            if (langs == null)
                return 0;

            var cur_en_US = langs.Find(x => x.id == "en-US");
            if (cur_en_US == null)
                return 0;

            if (path == null)
                path = app_dir;
            if (!path.EndsWith("\\"))
                path += '\\';

            // Build the list of all the current IDs we need to process
            var en_ids = new List<Id>();
            foreach (var kvp in cur_en_US.id_to_str)
                en_ids.Add(new Id(kvp.Key.group, kvp.Key.id));

            var added_ids = new List<Id>();
            var modified_ids = new List<Id>();

            if (old_en_US != null)
            {
                foreach (var id in cur_en_US.id_to_str.Keys)
                {
                    if (!old_en_US.id_to_str.ContainsKey(id))
                    {
                        // ID is not present in old -> added
                        //Console.WriteLine($"ADDED: {id} = {cur_en_US.id_to_str[id]}");
                        added_ids.Add(id);
                    }
                    else if (old_en_US.id_to_str[id] != cur_en_US.id_to_str[id])
                    {
                        // Ignore messages where we just removed the trailing \n
                        if (!old_en_US.id_to_str[id].EndsWith("\\n\""))
                        {
                            // ID exists in both but str has changed -> modified
                            //Console.WriteLine($"MODIFIED: {id} = {old_en_US.id_to_str[id]} → {cur_en_US.id_to_str[id]}");
                            modified_ids.Add(id);
                        }
                    }
                }
            }

            int nb_po_saved = 0;
            foreach (var lang in langs)
            {
                bool is_pot = (lang.id == "en-US");
                // Don't create the .pot if we are producing a merge
                if (is_pot && old_en_US != null)
                    continue;
                var target = path + (is_pot ? "rufus.pot" : lang.id + ".po");
                if (old_en_US != null)
                    Console.Write($"Computing differences and creating '{target}'... ");
                else
                    Console.Write($"Creating '{target}'... ");

                using (var writer = new StreamWriter(target, false, encoding))
                {
                    writer.WriteLine();
                    writer.WriteLine("msgid \"\"");
                    writer.WriteLine("msgstr \"\"");
                    writer.WriteLine($"\"Project-Id-Version: {((old_en_US != null) ? cur_en_US.version : lang.version)}\\n\"");
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

                    var dupes = new List<string>();

                    foreach(var id in en_ids)
                    {
                        var en_str = cur_en_US.sections[id.group].Find(x => x.id == id.id).str;
                        // Handle duplicate IDs
                        if (dupes.Contains(en_str))
                            continue;

                        writer.WriteLine();

                        var cid_list = cur_en_US.id_to_str.Where(x => x.Value == en_str).Select(x => x.Key);
                        foreach (var cid in cid_list)
                            writer.WriteLine($"#. • {cid}");
                        if (cid_list.Count() > 1)
                            dupes.Add(en_str);

                        if (cur_en_US.comments.ContainsKey(id))
                        {
                            writer.WriteLine("#.");
                            foreach (var comment in cur_en_US.comments[id].Split('\n'))
                                if (comment.Trim() != "")
                                    writer.WriteLine("#. " + comment);
                        }
                        if (!is_pot && lang.comments.ContainsKey(id))
                        {
                            foreach (var comment in lang.comments[id].Split('\n'))
                                if (comment.Trim() != "")
                                    writer.WriteLine("# " + comment);
                        }
                        // Flag the new/modified messages as requiring work
                        if ((old_en_US != null) && (added_ids.Contains(id) || modified_ids.Contains(id)))
                            writer.WriteLine("#, fuzzy");
                        string msg_str = lang.sections[id.group].Where(x => x.id == id.id).Select(x => x.str).FirstOrDefault();
                        // Special case for MSG_240, which we missed in the last round
                        if (id.group == "MSG" && id.id == "MSG_240" && msg_str == null)
                            writer.WriteLine("#, fuzzy");
                        if (msg_str == null)
                            msg_str = "\"\"";
                        if (is_pot)
                        {
                            writer.WriteLine($"msgid {msg_str}");
                            writer.WriteLine("msgstr \"\"");
                        }
                        else
                        {
                            writer.WriteLine($"msgid {en_str}");
                            writer.WriteLine($"msgstr {((msg_str == en_str) ? "\"\"" : msg_str)}");
                        }
                    }
                }
                nb_po_saved++;
                Console.WriteLine("DONE");
            }
            return nb_po_saved;
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
            Console.Write($"Importing data from '{file}'... ");
            bool is_pot = file.EndsWith(".pot");
            bool file_locked = true;
            string[] lines = null;
            // May get an I/O expection if Poedit is not done
            do
            {
                try
                {
                    lines = File.ReadAllLines(file);
                    file_locked = false;
                }
                catch (IOException)
                {
                    if (cancel_requested)
                        return null;
                    Thread.Sleep(100);
                }
            }
            while (file_locked);
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
                            var sep = header_line.IndexOf(":");
                            if (sep <= 0)
                            {
                                Console.WriteLine($"ERROR: Invalid header line '{header_line}'");
                                continue;
                            }
                            options.Add(header_line.Substring(0, sep).Trim(), header_line.Substring(sep + 1).Trim());
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
                else if ((is_pot && (data.StartsWith("#. "))) || (!is_pot && (data.StartsWith("# "))))
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
                                lang.comments.Add(id, "");
                                foreach (var comment in comments)
                                    lang.comments[id] += comment + "\n";
                            }
                            // Ignore messages that have the same translation as en-US
                            if (msg_data[0] == msg_data[1])
                                continue;
                            // Ignore blank translations
                            if (!is_pot && string.IsNullOrEmpty(msg_data[1]))
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

            Console.WriteLine(cancel_requested ? "CANCELLED" : "DONE");

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
                    var id = new Id(section, msg.id);
                    if (lang.comments.ContainsKey(id))
                    {
                        foreach (var l in lang.comments[id].Split('\n'))
                            if (l.Trim() != "")
                                writer.WriteLine($"# {l}");
                    }
                    writer.WriteLine($"t {msg.id} \"{msg.str}\"");
                }
            }
        }

        /// <summary>
        /// Update a rufus.loc section from a language element.
        /// </summary>
        /// <param name="lang">The Language elements to update.</param>
        /// <param name="path">(Optional) The path where 'rufus.loc' is located.</param>
        /// <returns>true on success, false on error.</returns>
        static bool UpdateLocFile(Language lang, string path = null)
        {
            if (lang == null)
                return false;
            if (path == null)
                path = app_dir;
            if (!path.EndsWith("\\"))
                path += '\\';

            var target = path + "rufus.loc";
            var lines = File.ReadAllLines(target);
            using (var writer = new StreamWriter(target, false, encoding))
            {
                bool skip = false;
                foreach (var line in lines)
                {
                    if (line.StartsWith($"# • v"))
                    {
                        var parts = line.Split('"');
                        if (parts.Count() < 2)
                        {
                            writer.WriteLine(line);
                            continue;
                        }
                        if (parts[1] != lang.id)
                        {
                            writer.WriteLine(line);
                            continue;
                        }
                        writer.WriteLine($"# • v{lang.version,-4} \"{lang.id}\" \"{lang.name}\"");
                        continue;
                    }
                    else if (line.StartsWith($"l \"{lang.id}\""))
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
        /// <param name="list">The list of Language elements.</param>
        /// <param name="path">(Optional) The path where the new 'rufus.loc' should be created.</param>
        /// <returns>true on success, false on error.</returns>
        static bool SaveLocFile(List<Language> list, string path = null)
        {
            if ((list == null) || (list.Count == 0))
                return false;
            if (path == null)
                path = app_dir;
            if (!path.EndsWith("\\"))
                path += '\\';
            var target = path + "rufus.loc";

            sw.Start();

            Console.WriteLine($"Creating '{target}':");
            using (var writer = new StreamWriter(target, false, encoding))
            {
                var notice = $"### Autogenerated by {app_name} {version_str} for use with Rufus - DO NOT EDIT!!! ###";
                var sep = new String('#', notice.Length);
                writer.WriteLine(sep);
                writer.WriteLine(notice);
                writer.WriteLine(sep);
                writer.WriteLine();
                writer.WriteLine("# List of all languages included in this file (with version)");
                foreach (var lang in list)
                {
                    writer.WriteLine($"# • v{lang.version, -4} \"{lang.id}\" \"{lang.name}\"");
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

            sw.Stop();
            Console.WriteLine($"{(cancel_requested ? "CANCELLED after" : "DONE in")}" +
                $" {sw.ElapsedMilliseconds / 1000.0}s.");
            sw.Reset();

            return true;
        }

        /// <summary>
        /// Validate a download URL by checking its HTTP status code.
        /// </summary>
        /// <param name="url">The URL to validate.</param>
        /// <returns>true if URL is acessible, false on error.</returns>
        static bool ValidateDownload(string url)
        {
            HttpStatusCode status = HttpStatusCode.InternalServerError;
            var uri = new Uri(url);
            WebRequest request = WebRequest.Create(uri);
            request.CachePolicy = new RequestCachePolicy(RequestCacheLevel.NoCacheNoStore);
            request.Method = "HEAD";

            // This is soooooooo retarded. Trying to simply read a 404 response throws a 404 *exception*?!?
            try
            {
                using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
                    status = response.StatusCode;
            }
            catch (WebException we)
            {
                HttpWebResponse response = we.Response as HttpWebResponse;
                if (response != null)
                {
                    status = response.StatusCode;
                    response.Close();
                }
            }
            request.Abort();
            switch (status)
            {
                case HttpStatusCode.OK:
                    return true;
                default:
                    Console.WriteLine($"Error downloading {url}: {(int)status} - {status}");
                    return false;
            }
        }

        /// <summary>
        /// Download a file as a string. Codepage is assumed to be UTF-8.
        /// </summary>
        /// <param name="url">The URL to download from.</param>
        /// <returns>The downloaded string or null on error.</returns>
        static string DownloadString(string url)
        {
            string str = null;

            if (!ValidateDownload(url))
                return null;

            using (WebClient wc = new WebClient())
            {
                try
                {
                    str = System.Text.Encoding.UTF8.GetString(wc.DownloadData(new Uri(url)));
                }
                catch (Exception e)
                {
                    Console.WriteLine("ERROR: " + e.Message);
                    return null;
                }
            }
            return str;
        }

        /// <summary>
        /// Download a file.
        /// </summary>
        /// <param name="url">The URL to download from.</param>
        /// <param name="dest">(Optional) The destination file.
        /// If not provided the file is saved in the current directory, using the last part of the URL as its name.</param>
        /// <returns>true if the download was complete, false otherwise.</returns>
        static bool DownloadFile(string url, string dest = null)
        {
            download_status = 0;
            in_progress = false;
            var uri = new Uri(url);

            if (dest == null)
                dest = url.Split('/').Last();

            if (!ValidateDownload(url))
                return false;

            console_x_pos = Console.CursorLeft;
            using (WebClient wc = new WebClient())
            {
                wc.CachePolicy = new RequestCachePolicy(RequestCacheLevel.NoCacheNoStore);
                wc.DownloadFileCompleted += new AsyncCompletedEventHandler(DownloadCompleted);
                wc.DownloadProgressChanged += new DownloadProgressChangedEventHandler(DownloadProgress);

                sw.Start();

                try
                {
                    wc.DownloadFileAsync(uri, dest);
                }
                catch (Exception e)
                {
                    Console.WriteLine(" Error: " + e.Message);
                    return false;
                }
            }
            while (download_status == 0)
                Thread.Sleep(100);

            Console.WriteLine();
            if (download_status == 1)
                return true;

            Console.WriteLine($"Download has {((download_status == 2) ? "been cancelled" : "failed")}.");
            return false;
        }

        // Progress event used by DownloadFile()
        static void DownloadProgress(object sender, DownloadProgressChangedEventArgs e)
        {
            if (cancel_requested)
            {
                ((WebClient)sender).CancelAsync();
                return;
            }

            // Prevent this call from being re-entrant
            if (in_progress)
                return;
            in_progress = true;

            speed = (e.BytesReceived / 1024d / sw.Elapsed.TotalSeconds);
            Console.SetCursorPosition(console_x_pos, Console.CursorTop);
            Console.Write($"{e.ProgressPercentage.ToString("0.0"), 5}% ({speed.ToString("0.00")} KB/s)");
            in_progress = false;
        }

        // Completed event used by DownloadFile()
        static void DownloadCompleted(object sender, AsyncCompletedEventArgs e)
        {
            Console.SetCursorPosition(console_x_pos, Console.CursorTop);
            sw.Reset();
            if (e.Error != null)
            {
                Console.Write("Error: " + e.Error.Message);
                download_status = 3;
            }
            else
            {
                Console.Write($"{100.0d.ToString("0.0"),5}% ({speed.ToString("0.00")} KB/s) {(e.Cancelled ? "CANCELLED" : "DONE")}");
                download_status = (e.Cancelled) ? 2 : 1;
            }
        }

        /// <summary>
        /// Prompt a user for a Y/N question.
        /// </summary>
        /// <param name="question">The question string.</param>
        /// <returns>true if the user answered 'Y'.</returns>
        static bool PromptForQuestion(string question)
        {
            ConsoleKey response;
            do
            {
                Console.Write(question + " [y/n] ");
                console_x_pos = Console.CursorLeft - 6;
                response = Console.ReadKey(false).Key;
                if (response != ConsoleKey.Enter)
                    Console.WriteLine();
            } while (response != ConsoleKey.Y && response != ConsoleKey.N);
            // Flush
            while (Console.KeyAvailable)
                Console.ReadKey(true);
            return (response == ConsoleKey.Y);
        }

        // Event handler for FileSystemWatcher. As usual, this is a completely BACKWARDS
        // implementation by Microsoft that has to be worked around with timers and stuff...
        private static void OnChanged(object source, FileSystemEventArgs e)
        {
            if (in_on_change)
                return;
            in_on_change = true;
            FileInfo file = new FileInfo(e.FullPath);
            FileStream stream = null;
            if (file.LastWriteTime >= last_changed.AddMilliseconds(250))
            {
                // File may still be locked by Poedit => detect that
                bool file_locked = true;
                do
                {
                    try
                    {
                        stream = file.Open(FileMode.Open, FileAccess.Read, FileShare.None);
                        file_locked = false;
                    }
                    catch (IOException)
                    {
                        if (cancel_requested)
                            break;
                        Thread.Sleep(100);
                    }
                    finally
                    {
                        if (!file_locked)
                        {
                            if (stream != null)
                                stream.Close();
                            last_changed = file.LastWriteTime;
                            Console.Write(file.LastWriteTime.ToLongTimeString() + " - ");
                            UpdateLocFile(ParsePoFile(e.FullPath));
                        }
                    }
                }
                while (file_locked);
            }
            in_on_change = false;
        }

        //
        // Main entrypoint.
        //
        [STAThread]
        static void Main(string[] args)
        {
            // Fix needed for Windows 7 to download from github SSL
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;

            // Also set the Console width to something that can accomodate us
            if (Console.WindowWidth < 100)
                Console.SetWindowSize(100, Console.WindowHeight);

            bool maintainer_mode = false;
            Console.OutputEncoding = System.Text.Encoding.UTF8;
            Console.CancelKeyPress += delegate (object sender, ConsoleCancelEventArgs e) {
                e.Cancel = true;
                cancel_requested = true;
            };
            Console.WriteLine($"{app_name} {version_str} - Poedit to rufus.loc conversion utility");
            Console.WriteLine();

            string loc_url = "https://github.com/pbatard/rufus/raw/master/res/loc/rufus.loc";
            string ver_url = "https://rufus.ie/Loc.ver";
            string rufus_url = null;
            string rufus_file = null;
            string download_url = null;
            string po_file = null;
            string loc_file = null;
            string id = "", name = "";
            int[] update_version = new int[2] { 0, 0 };
            var list = new List<string[]>();
            int index = -1;

            // Parse parameters
            foreach (var arg in args)
            {
                if (arg.Contains("i"))
                {
                    maintainer_mode = true;
                    goto Import;
                }
                else if (arg.Contains("l"))
                {
                    loc_file = @"..\rufus.loc";
                    foreach (var line in File.ReadAllLines(loc_file))
                    {
                        if (line.StartsWith("l "))
                        {
                            var el = line.Split('\"');
                            id = el[1];
                            name = el[3].Split('(')[0].Trim();
                        }
                        else if (line.StartsWith("v "))
                        {
                            list.Add(new string[] { name, id, line.Substring(2) });
                        }
                    }
                    maintainer_mode = true;
                    goto Menu;
                }
            }

            // Check for updates of this application
            Console.Write("Downloading latest application data... ");
            var ver = DownloadString(ver_url);
            if (ver == null)
            {
                Console.WriteLine("ERROR: Could not access application data.");
                goto Exit;
            }
            foreach (var line in ver.Split('\n'))
            {
                var parts = line.Split('=');
                if (parts.Count() < 2)
                    continue;
                switch(parts[0].Trim())
                {
                    case "version":
                        Int32.TryParse(parts[1].Split('.')[0], out update_version[0]);
                        Int32.TryParse(parts[1].Split('.')[1], out update_version[1]);
                        break;
                    case "download_url":
                        download_url = parts[1].Trim();
                        break;
                    case "rufus_url":
                        rufus_url = parts[1].Trim();
                        break;
                }
            }
            if ((download_url == null) || (rufus_url == null) || (update_version[0] == 0))
            {
                Console.WriteLine("FAILED");
                goto Exit;
            }
            Console.WriteLine("DONE");

            // Download new version
            if ((update_version[0] > version[0]) || ((update_version[0] == version[0]) && (update_version[1] > version[1])))
            {
                Console.WriteLine();
                if (PromptForQuestion("A new version of this application is available! Do you want to download it?"))
                {
                    if (DownloadFile(download_url))
                    {
                        Console.WriteLine("Now re-launch this program using the latest version.");
                        goto Exit;
                    }
                    Console.WriteLine("Download failed.");
                }
            }

            if (rufus_url != null)
            {
                // Download the latest version of Rufus to use for translations
                rufus_file = rufus_url.Split('/').Last();
                Console.Write($"Checking for the presence of '{rufus_file}'... ");
                if (File.Exists(rufus_file))
                {
                    Console.WriteLine("FOUND");
                }
                else
                {
                    var rufus_name = rufus_url.Split('/').Last();
                    Console.WriteLine("MISSING");
                    Console.WriteLine($"{rufus_name} doesn't exist in your translation directory.");
                    Console.WriteLine("This is the required version to validate your changes.");
                    if (PromptForQuestion($"Do you want to download {rufus_name}?")) {
                        Console.SetCursorPosition(console_x_pos, Console.CursorTop - 1);
                        DownloadFile(rufus_url);
                    }
                }
            }

            if (!maintainer_mode)
            {
                // Download the latest loc file
                Console.Write("Downloading the latest loc file... ");
                if (!DownloadFile(loc_url))
                    goto Exit;
            }
            else
            {
                var local_loc = @"C:\rufus\res\loc\rufus.loc";
                Console.Write($"Copying loc file from '{local_loc}'... ");
                File.Copy(local_loc, "rufus.loc", true);
            }

            loc_file = loc_url.Split('/').Last();
            // Convert to CRLF and get all the language ids
            var lines = File.ReadAllLines(loc_file);
            using (var writer = new StreamWriter(loc_file, false, encoding))
            {
                foreach (var line in lines)
                {
                    if (line.StartsWith("l "))
                    {
                        var el = line.Split('\"');
                        id = el[1];
                        name = el[3].Split('(')[0].Trim();
                    }
                    else if (line.StartsWith("v "))
                    {
                        list.Add(new string[] { name, id, line.Substring(2) });
                    }
                    writer.WriteLine(line);
                }
            }

Menu:
            Console.WriteLine();
            Console.WriteLine("Please enter the number of the language you want to edit or 'q' to quit:");
            Console.WriteLine();
            int split = list.Count / 2;
            for (int i = 1; i < split + 1; i++)
            {
                name = $"{list[i][0]} ({list[i][1]})";
                Console.Write($"[{i.ToString("00")}] {name,-29} {$"(v{list[i][2]})",-7}");
                name = $"{list[i + split][0]} ({list[i + split][1]})";
                Console.WriteLine($"  |  [{(i + split).ToString("00")}] {name,-29} {$"(v{list[i + split][2]})",-7}");
            }
            Console.WriteLine();

Retry:
            Console.Write("> ");
            string input = Console.ReadLine();
            if ((input == null) || (input.StartsWith("q")))
                goto Exit;
            if (!Int32.TryParse(input, out index) || (index <= 0) || (index > list.Count))
            {
                if (input.StartsWith("m"))
                    goto Menu;
                Console.WriteLine("Invalid selection (Type 'm' to display the menu again)");
                goto Retry;
            }

            Console.SetCursorPosition(0, Console.CursorTop - 1);
            Console.WriteLine($"{list[index][0]} was selected.");
            Console.WriteLine();
            po_file = $"{list[index][1]}.po";

            Language old_en_US = null;
            if (list[index][2] == list[0][2])
            {
                if (!maintainer_mode)
                {
                    Console.WriteLine("Note: This language is already at the most recent version!");
                    if (!PromptForQuestion("Do you still want to edit it?"))
                        goto Exit;
                }
            }
            else
            {
                var old_loc_file = $"rufus-{list[index][2]}.loc";
                Console.WriteLine($"Note: This language is at v{list[index][2]} but the English base it at v{list[0][2]}.");
                Console.Write($"Checking for the presence of '{old_loc_file}' to compute the differences... ");
                if (File.Exists(old_loc_file))
                {
                    Console.WriteLine("FOUND");
                }
                else
                {
                    Console.WriteLine("MISSING");
                    Console.Write($"Downloading '{old_loc_file}'... ");
                    var url = "https://github.com/pbatard/rufus/releases/tag/v" + list[index][2];
                    var str = DownloadString(url);
                    if (str == null)
                        goto Exit;
                    var sha = str.Substring(str.IndexOf("/pbatard/rufus/commit/") + 22, 40);
                    // TODO: Remove this once everyone has upgraded past 3.2
                    string loc_dir = ((list[index][2][0] == '2') || ((list[index][2][0] == '3') && (list[index][2][2] == '0'))) ? "localization" : "loc";
                    url = "https://github.com/pbatard/rufus/raw/" + sha + "/res/" + loc_dir + "/rufus.loc";
                    if (!DownloadFile(url, old_loc_file))
                        goto Exit;
                }
                var old_langs = ParseLocFile(old_loc_file, "en-US");
                if ((old_langs == null) || (old_langs.Count != 1))
                {
                    Console.WriteLine("Error: Unable to get en-US data from previous loc file.");
                    goto Exit;
                }
                old_en_US = old_langs[0];
            }

            if (File.Exists(po_file) && !maintainer_mode)
            {
                if (!PromptForQuestion($"A '{po_file}' file already exists. Do you want to overwrite it? (If unsure, say 'y')"))
                    goto Exit;
            }
            if (CreatePoFiles(ParseLocFile(loc_file, list[index][1]), old_en_US) < 1)
            {
                Console.WriteLine("Failed to create PO file");
                goto Exit;
            }

            if (maintainer_mode)
                goto Exit;

            // Watch for file modifications
            FileSystemWatcher watcher = new FileSystemWatcher();
            watcher.Path = app_dir;
            watcher.NotifyFilter = NotifyFilters.LastAccess | NotifyFilters.LastWrite;
            watcher.Filter = po_file;
            watcher.Changed += new FileSystemEventHandler(OnChanged);
            watcher.EnableRaisingEvents = true;

            // Open the file in PoEdit if we can
            var poedit = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86) + @"\Poedit\Poedit.exe";
            if (File.Exists(poedit))
            {
                Console.WriteLine();
                //                Console.WriteLine("Please press any key to launch Poedit and edit the PO file.");
                Console.WriteLine("*************************************************************************************");
                Console.WriteLine($"* The {list[index][0]} translation file ({list[index][1]}) is now ready to be edited in Poedit.");
                Console.WriteLine("* Please look for entries highlited in orange - they are the ones requiring an update.");
                Console.WriteLine("*");
                Console.WriteLine("* Whenever you save your changes in Poedit, a new 'rufus.loc' will be generated so");
                Console.WriteLine($"* that you can test it with '{rufus_file}' in the same directory.");
                Console.WriteLine("*");
                Console.WriteLine("* PLEASE DO NOT CLOSE THIS CONSOLE APPLICATION - IT NEEDS TO RUN IN THE BACKGROUND!");
                Console.WriteLine("* Instead, when you are done editing your translation, simply close Poedit.");
                Console.WriteLine("*************************************************************************************");
                WaitForKey($"Press any key to open '{po_file}' in Poedit...");

                Process process = new Process();
                process.StartInfo.FileName = poedit;
                process.StartInfo.WorkingDirectory = app_dir;
                process.StartInfo.Arguments = po_file;
                process.StartInfo.WindowStyle = ProcessWindowStyle.Maximized;
                if (!process.Start())
                {
                    Console.WriteLine("Error: Could not launch PoEdit");
                    goto Exit;
                }
                Console.SetCursorPosition(0, Console.CursorTop - 1);
                Console.WriteLine("Running Poedit...                                 ");
                DateTime launch_date = DateTime.Now;
                process.WaitForExit();
                Console.WriteLine($"Poedit {((DateTime.Now - launch_date).Milliseconds < 100? "is already running (?)..." : "was closed.")}");
                // Delete the .mo files which we don't need
                var dir = new DirectoryInfo(app_dir);
                foreach (var file in dir.EnumerateFiles("*.mo"))
                    file.Delete();
            }
            else
            {
                Console.WriteLine("Poedit was not found. You will have to launch it and open the");
                Console.WriteLine($"'{po_file}' file manually.");
            }

            WaitForKey("Now press any key to launch your e-mail client and exit this application...");

            if ((list.Count >= 2) && (index >= 0))
            {
                Process.Start($"mailto:pete@akeo.ie?subject=Rufus {list[index][0]} translation v{list[0][2]} update" +
                    $"&body=Hi Pete,%0D%0A%0D%0APlease find attached the latest {list[index][0]} translation." +
                    $"%0D%0A%0D%0A<PLEASE ATTACH '{app_dir}{po_file}' AND REMOVE THIS LINE>" +
                    $"%0D%0A%0D%0ARegards,");
            }
            return;

Import:
            string file_name;
            OpenFileDialog file_dialog = new OpenFileDialog();
            file_dialog.InitialDirectory = app_dir;
            file_dialog.Filter = "PO files (*.po)|*.po|All files (*.*)|*.*";
            file_dialog.ShowDialog();
            file_name = file_dialog.FileName;
            Console.WriteLine(file_name);
            UpdateLocFile(ParsePoFile(file_name), app_dir + @"..\");

Exit:
            if (!maintainer_mode)
                WaitForKey("Press any key to exit...");
        }
    }
}
