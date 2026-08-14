using System;
using System.Collections.Generic;
using System.Data;
using System.Data.Common;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;

namespace MHXXDexDump
{
    internal static class Runner
    {
        private const string MarkerName = ".mhxx-dex-dump";
        private const string ExpectedExeSha256 = "5e0508b80b02565c5c7217861c315911878896567b07659631c34b7f7dae46c7";
        private static readonly UTF8Encoding Utf8 = new UTF8Encoding(false);

        public static int Main(string[] args)
        {
            try
            {
                if (args.Length != 2)
                {
                    Console.Error.WriteLine("usage: MHXXDexDump <dex-dir> <out-dir>");
                    return 2;
                }

                Run(Path.GetFullPath(args[0]), Path.GetFullPath(args[1]));
                return 0;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("fatal: " + Unwrap(ex));
                return 1;
            }
        }

        private static void Run(string dexDir, string outDir)
        {
            string exePath = Path.Combine(dexDir, "MHXX Dex.exe");
            if (!File.Exists(exePath))
                throw new FileNotFoundException("MHXX Dex.exe was not found", exePath);

            string exeSha256 = Sha256(exePath);
            if (!String.Equals(exeSha256, ExpectedExeSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Unsupported MHXX Dex executable: expected SHA-256 "
                    + ExpectedExeSha256 + ", found " + exeSha256);

            PrepareOutput(outDir);
            // Mark ownership immediately so an interrupted run can be safely repeated.
            File.WriteAllText(Path.Combine(outDir, MarkerName), "MHXX Dex 1.0 runtime dump\n", Utf8);
            Environment.CurrentDirectory = dexDir;
            AppDomain.CurrentDomain.AssemblyResolve += delegate(object sender, ResolveEventArgs eventArgs)
            {
                string fileName = new AssemblyName(eventArgs.Name).Name + ".dll";
                string candidate = Path.Combine(dexDir, fileName);
                return File.Exists(candidate) ? Assembly.LoadFrom(candidate) : null;
            };

            Console.WriteLine("Loading MHXX Dex 1.0: " + exePath);
            Assembly assembly = Assembly.LoadFrom(exePath);
            if (assembly.GetName().Name != "MHXX Dex" || assembly.GetName().Version.ToString() != "1.0.0.0")
                throw new InvalidOperationException("Unexpected assembly identity: " + assembly.FullName);
            Type dataType = FindDataType(assembly);
            if (dataType == null)
                throw new InvalidOperationException("Could not find the MHXX Dex data type.");

            int tableFields = CountStaticFields(dataType, typeof(DataTable));
            int adapterFields = dataType.GetFields(AllStatic).Count(f => LooksLikeAdapter(f.FieldType));
            if (tableFields != 131 || adapterFields != 131)
                throw new InvalidOperationException("Unexpected MHXX Dex data layout: DataTable=" + tableFields + ", adapters=" + adapterFields);

            // These are the exact initialization calls made by this pinned executable before
            // constructing its main form. The obfuscated names are intentionally verified.
            InvokeExactNoArg(assembly, "\u1703", "\u1700", typeof(void));
            InvokeExactNoArg(assembly, "\u1704", "\u1718", typeof(void));

            Console.WriteLine("Database initialized; dumping live tables.");
            List<TableManifest> runtimeTables = DumpRuntimeTables(dataType, outDir);
            DirectSqlManifest direct = DumpDirectSql(dataType, outDir);
            WriteManifest(dexDir, outDir, dataType, runtimeTables, direct);
            DbConnection connection = FindConnection(dataType);
            if (connection != null && connection.State != ConnectionState.Closed)
                connection.Close();

            Console.WriteLine("Dump complete: " + outDir);
        }

        private const BindingFlags AllStatic = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;

        private static void InvokeExactNoArg(Assembly assembly, string typeName, string methodName, Type returnType)
        {
            Type type = assembly.GetType(typeName, false);
            if (type == null)
                throw new InvalidOperationException("Required MHXX Dex type was not found: " + Codepoints(typeName));

            MethodInfo method = type.GetMethods(AllStatic)
                .SingleOrDefault(m => m.Name == methodName && m.ReturnType == returnType && m.GetParameters().Length == 0);
            if (method == null)
                throw new InvalidOperationException("Required MHXX Dex method was not found: " + Codepoints(typeName) + "." + Codepoints(methodName));

            Console.WriteLine("Invoke " + Codepoints(typeName) + "." + Codepoints(methodName));
            method.Invoke(null, null);
        }

        private static Type FindDataType(Assembly assembly)
        {
            return assembly.GetTypes()
                .Select(t => new
                {
                    Type = t,
                    Tables = CountStaticFields(t, typeof(DataTable)),
                    Adapters = t.GetFields(AllStatic).Count(f => LooksLikeAdapter(f.FieldType))
                })
                .OrderByDescending(x => x.Tables + x.Adapters)
                .Where(x => x.Tables > 0 && x.Adapters > 0)
                .Select(x => x.Type)
                .FirstOrDefault();
        }

        private static int CountStaticFields(Type type, Type fieldType)
        {
            return type.GetFields(AllStatic).Count(f => f.FieldType == fieldType);
        }

        private static bool LooksLikeAdapter(Type type)
        {
            return type != null && type.FullName != null && type.FullName.IndexOf("DataAdapter", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private static List<TableManifest> DumpRuntimeTables(Type dataType, string outDir)
        {
            string tablesDir = Path.Combine(outDir, "tables");
            Directory.CreateDirectory(tablesDir);
            FieldInfo[] fields = dataType.GetFields(AllStatic).OrderBy(f => f.MetadataToken).ToArray();
            List<TableManifest> result = new List<TableManifest>();
            int index = 0;

            foreach (FieldInfo field in fields.Where(f => f.FieldType == typeof(DataTable)))
            {
                DataTable table = field.GetValue(null) as DataTable;
                if (table == null)
                    continue;

                string key = index.ToString("000", CultureInfo.InvariantCulture) + "_" + SafeName(field.Name);
                string relativePath = "tables/" + key + ".csv";
                WriteCsv(table, Path.Combine(outDir, relativePath));

                FieldInfo adapterField = fields.FirstOrDefault(f => f.Name == field.Name && LooksLikeAdapter(f.FieldType));
                string query = adapterField == null ? null : ReadSelectCommand(adapterField.GetValue(null));
                result.Add(new TableManifest(field.Name, table, relativePath, query));
                index++;
            }

            return result;
        }

        private static string ReadSelectCommand(object adapter)
        {
            if (adapter == null)
                return null;
            try
            {
                PropertyInfo select = adapter.GetType().GetProperty("SelectCommand", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
                object command = select == null ? null : select.GetValue(adapter, null);
                if (command == null)
                    return null;
                PropertyInfo text = command.GetType().GetProperty("CommandText", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
                return text == null ? null : text.GetValue(command, null) as string;
            }
            catch
            {
                return null;
            }
        }

        private static DirectSqlManifest DumpDirectSql(Type dataType, string outDir)
        {
            string sqlDir = Path.Combine(outDir, "direct_sql");
            Directory.CreateDirectory(sqlDir);
            DbConnection connection = FindConnection(dataType);
            if (connection == null)
                throw new InvalidOperationException("The live SQLite connection was not found.");
            if (connection.State != ConnectionState.Open)
                connection.Open();

            DataTable master = Query(connection, "SELECT type, name, tbl_name, sql FROM sqlite_master WHERE type IN ('table','view') ORDER BY type,name");
            WriteCsv(master, Path.Combine(sqlDir, "sqlite_master.csv"));

            List<DirectSqlTable> dumped = new List<DirectSqlTable>();
            List<string> failures = new List<string>();
            foreach (DataRow row in master.Rows)
            {
                string name = Convert.ToString(row["name"], CultureInfo.InvariantCulture);
                try
                {
                    DataTable table = Query(connection, "SELECT * FROM " + QuoteIdentifier(name));
                    WriteCsv(table, Path.Combine(sqlDir, SafeName(name) + ".csv"));
                    string sql = Convert.ToString(row["sql"], CultureInfo.InvariantCulture);
                    dumped.Add(new DirectSqlTable(name, "direct_sql/" + SafeName(name) + ".csv", table.Rows.Count, sql));
                    Console.WriteLine("SQL " + name + ": " + table.Rows.Count + " rows");
                }
                catch (Exception ex)
                {
                    failures.Add(name + ": " + Unwrap(ex).Message);
                }
            }

            if (dumped.Count == 0)
                throw new InvalidOperationException("No editor-related SQLite tables were exported.");
            File.WriteAllLines(Path.Combine(sqlDir, "failures.txt"), failures.ToArray(), Utf8);
            return new DirectSqlManifest(master.Rows.Count, dumped, failures);
        }

        private static DbConnection FindConnection(Type dataType)
        {
            foreach (FieldInfo field in dataType.GetFields(AllStatic))
            {
                if (!typeof(DbConnection).IsAssignableFrom(field.FieldType))
                    continue;
                DbConnection connection = field.GetValue(null) as DbConnection;
                if (connection != null)
                    return connection;
            }
            return null;
        }

        private static DataTable Query(DbConnection connection, string sql)
        {
            using (DbCommand command = connection.CreateCommand())
            {
                command.CommandText = sql;
                using (DbDataReader reader = command.ExecuteReader())
                {
                    DataTable table = new DataTable();
                    HashSet<string> names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    for (int i = 0; i < reader.FieldCount; i++)
                    {
                        string baseName = reader.GetName(i);
                        if (String.IsNullOrEmpty(baseName)) baseName = "column_" + i.ToString(CultureInfo.InvariantCulture);
                        string name = baseName;
                        int suffix = 2;
                        while (!names.Add(name))
                            name = baseName + "_" + (suffix++).ToString(CultureInfo.InvariantCulture);
                        Type type;
                        try { type = reader.GetFieldType(i); }
                        catch { type = typeof(object); }
                        table.Columns.Add(name, type ?? typeof(object));
                    }

                    while (reader.Read())
                    {
                        DataRow row = table.NewRow();
                        for (int i = 0; i < reader.FieldCount; i++)
                            row[i] = reader.IsDBNull(i) ? DBNull.Value : reader.GetValue(i);
                        table.Rows.Add(row);
                    }
                    return table;
                }
            }
        }

        private static string QuoteIdentifier(string value)
        {
            return "[" + value.Replace("]", "]]" ) + "]";
        }

        private static void WriteCsv(DataTable table, string path)
        {
            using (StreamWriter writer = new StreamWriter(path, false, Utf8))
            {
                for (int i = 0; i < table.Columns.Count; i++)
                {
                    if (i > 0) writer.Write(',');
                    writer.Write(Csv(table.Columns[i].ColumnName));
                }
                writer.Write('\n');

                foreach (DataRow row in table.Rows)
                {
                    for (int i = 0; i < table.Columns.Count; i++)
                    {
                        if (i > 0) writer.Write(',');
                        writer.Write(Csv(FormatValue(row[i])));
                    }
                    writer.Write('\n');
                }
            }
        }

        private static string FormatValue(object value)
        {
            if (value == null || value == DBNull.Value) return String.Empty;
            byte[] bytes = value as byte[];
            if (bytes != null) return BitConverter.ToString(bytes).Replace("-", String.Empty);
            IFormattable formattable = value as IFormattable;
            return formattable == null ? value.ToString() : formattable.ToString(null, CultureInfo.InvariantCulture);
        }

        private static string Csv(string value)
        {
            if (value == null) return String.Empty;
            bool quote = value.IndexOfAny(new[] { ',', '"', '\r', '\n' }) >= 0;
            string escaped = value.Replace("\"", "\"\"");
            return quote ? "\"" + escaped + "\"" : escaped;
        }

        private static void WriteManifest(string dexDir, string outDir, Type dataType, List<TableManifest> tables, DirectSqlManifest direct)
        {
            string[] keyFiles = { "MHXX Dex.exe", "System.Data.SQLite.dll", "System.Data.SQLite.Interop.dll", "System.Data.SQLite.Linq.dll", "System.Data.SQLite.EF6.dll", "winbind.dll" };
            List<string> files = new List<string>();
            foreach (string name in keyFiles)
            {
                string path = Path.Combine(dexDir, name);
                if (File.Exists(path))
                    files.Add("{\"name\":" + Json(name) + ",\"size\":" + new FileInfo(path).Length.ToString(CultureInfo.InvariantCulture) + ",\"sha256\":" + Json(Sha256(path)) + "}");
            }

            string json = "{\n"
                + "  \"format\": \"mhxx-dex-runtime-dump-v1\",\n"
                + "  \"source\": \"MHXX Dex 1.0\",\n"
                + "  \"dataType\": " + Json(dataType.FullName) + ",\n"
                + "  \"sourceFiles\": [" + String.Join(",", files.ToArray()) + "],\n"
                + "  \"runtimeTables\": [\n    " + String.Join(",\n    ", tables.Select(t => t.ToJson()).ToArray()) + "\n  ],\n"
                + "  \"sqliteObjectCount\": " + direct.ObjectCount.ToString(CultureInfo.InvariantCulture) + ",\n"
                + "  \"directSqlTables\": [" + String.Join(",", direct.Dumped.Select(t => Json(t.Name)).ToArray()) + "],\n"
                + "  \"directSqlObjects\": [" + String.Join(",", direct.Dumped.Select(t => t.ToJson()).ToArray()) + "],\n"
                + "  \"directSqlFailures\": [" + String.Join(",", direct.Failures.Select(Json).ToArray()) + "]\n"
                + "}\n";
            File.WriteAllText(Path.Combine(outDir, "manifest.json"), json, Utf8);
        }

        private static void PrepareOutput(string outDir)
        {
            string marker = Path.Combine(outDir, MarkerName);
            if (Directory.Exists(outDir))
            {
                string[] entries = Directory.GetFileSystemEntries(outDir);
                if (entries.Length > 0 && !File.Exists(marker))
                    throw new InvalidOperationException("Refusing to replace an unmarked non-empty output directory: " + outDir);
                foreach (string known in new[] { "tables", "direct_sql" })
                {
                    string path = Path.Combine(outDir, known);
                    if (Directory.Exists(path)) Directory.Delete(path, true);
                }
                foreach (string known in new[] { "manifest.json", MarkerName })
                {
                    string path = Path.Combine(outDir, known);
                    if (File.Exists(path)) File.Delete(path);
                }
            }
            else
            {
                Directory.CreateDirectory(outDir);
            }
        }

        private static string SafeName(string value)
        {
            StringBuilder result = new StringBuilder();
            foreach (char c in value)
                result.Append(Char.IsLetterOrDigit(c) || c == '-' || c == '_' ? c : '_');
            return result.Length == 0 ? "unnamed" : result.ToString();
        }

        private static string Sha256(string path)
        {
            using (SHA256 sha = SHA256.Create())
            using (FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", String.Empty).ToLowerInvariant();
        }

        private static string Codepoints(string value)
        {
            return String.Join(" ", value.Select(c => "U+" + ((int)c).ToString("X4", CultureInfo.InvariantCulture)).ToArray());
        }

        private static string Json(string value)
        {
            if (value == null) return "null";
            StringBuilder result = new StringBuilder("\"");
            foreach (char c in value)
            {
                switch (c)
                {
                    case '\\': result.Append("\\\\"); break;
                    case '"': result.Append("\\\""); break;
                    case '\n': result.Append("\\n"); break;
                    case '\r': result.Append("\\r"); break;
                    case '\t': result.Append("\\t"); break;
                    default:
                        if (c < 32) result.Append("\\u" + ((int)c).ToString("X4", CultureInfo.InvariantCulture));
                        else result.Append(c);
                        break;
                }
            }
            return result.Append('"').ToString();
        }

        private static Exception Unwrap(Exception ex)
        {
            while ((ex is TargetInvocationException || ex is TypeInitializationException) && ex.InnerException != null)
                ex = ex.InnerException;
            return ex;
        }

        private sealed class TableManifest
        {
            private readonly string field;
            private readonly DataTable table;
            private readonly string file;
            private readonly string query;

            public TableManifest(string field, DataTable table, string file, string query)
            {
                this.field = field;
                this.table = table;
                this.file = file;
                this.query = query;
            }

            public string ToJson()
            {
                string columns = String.Join(",", table.Columns.Cast<DataColumn>().Select(c => "{\"name\":" + Json(c.ColumnName) + ",\"type\":" + Json(c.DataType.FullName) + "}").ToArray());
                return "{\"field\":" + Json(field) + ",\"fieldCodepoints\":" + Json(Codepoints(field)) + ",\"tableName\":" + Json(table.TableName)
                    + ",\"file\":" + Json(file) + ",\"rows\":" + table.Rows.Count.ToString(CultureInfo.InvariantCulture)
                    + ",\"columns\":[" + columns + "]" + (String.IsNullOrEmpty(query) ? String.Empty : ",\"query\":" + Json(query)) + "}";
            }
        }

        private sealed class DirectSqlManifest
        {
            public readonly int ObjectCount;
            public readonly List<DirectSqlTable> Dumped;
            public readonly List<string> Failures;

            public DirectSqlManifest(int objectCount, List<DirectSqlTable> dumped, List<string> failures)
            {
                ObjectCount = objectCount;
                Dumped = dumped;
                Failures = failures;
            }
        }

        private sealed class DirectSqlTable
        {
            public readonly string Name;
            private readonly string file;
            private readonly int rows;
            private readonly string sql;

            public DirectSqlTable(string name, string file, int rows, string sql)
            {
                Name = name;
                this.file = file;
                this.rows = rows;
                this.sql = sql;
            }

            public string ToJson()
            {
                return "{\"name\":" + Json(Name) + ",\"file\":" + Json(file)
                    + ",\"rows\":" + rows.ToString(CultureInfo.InvariantCulture)
                    + ",\"sql\":" + Json(sql) + "}";
            }
        }
    }
}
