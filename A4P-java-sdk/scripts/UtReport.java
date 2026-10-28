import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.time.ZonedDateTime;
import java.util.*;
import javax.xml.XMLConstants;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.transform.*;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;
import org.w3c.dom.*;

/** Dependency-free Surefire report renderer, compiled with JDK 17 or later. */
class UtReport {
    record Result(String status, String name, String duration, String detail) {}

    static String escape(String text) {
        return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
                .replace("\"", "&quot;").replace("'", "&#39;");
    }

    public static void main(String[] args) throws Exception {
        Path project = Path.of(args[0]);
        var factory = DocumentBuilderFactory.newInstance();
        factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true);
        factory.setAttribute(XMLConstants.ACCESS_EXTERNAL_DTD, "");
        factory.setAttribute(XMLConstants.ACCESS_EXTERNAL_SCHEMA, "");
        var builder = factory.newDocumentBuilder();
        var document = builder.newDocument();
        var suites = document.createElement("testsuites");
        document.appendChild(suites);
        List<Result> results = new ArrayList<>();
        List<String> warnings = new ArrayList<>();
        Map<String, Set<String>> testEnvironment = new LinkedHashMap<>();
        for (String key : List.of("os.name", "os.version", "java.version")) {
            testEnvironment.put(key, new LinkedHashSet<>());
        }
        Path reports = project.resolve("target/surefire-reports");
        if (Files.isDirectory(reports)) {
            List<Path> files;
            try (var stream = Files.list(reports)) {
                files = stream.filter(p -> p.getFileName().toString().matches("TEST-.*\\.xml"))
                        .sorted().toList();
            }
            for (Path file : files) {
                Element suite;
                try {
                    suite = builder.parse(file.toFile()).getDocumentElement();
                } catch (Exception ex) {
                    warnings.add(file.getFileName() + ": " + ex.getMessage());
                    continue;
                }
                suites.appendChild(document.importNode(suite, true));
                var properties = suite.getElementsByTagName("property");
                for (int i = 0; i < properties.getLength(); i++) {
                    Element property = (Element) properties.item(i);
                    var values = testEnvironment.get(property.getAttribute("name"));
                    if (values != null && !property.getAttribute("value").isBlank()) {
                        values.add(property.getAttribute("value"));
                    }
                }
                var cases = suite.getElementsByTagName("testcase");
                for (int i = 0; i < cases.getLength(); i++) {
                    Element test = (Element) cases.item(i);
                    String status = "Passed";
                    StringJoiner detail = new StringJoiner("\n\n");
                    for (String tag : List.of("failure", "error", "skipped")) {
                        var nodes = test.getElementsByTagName(tag);
                        if (nodes.getLength() == 0) continue;
                        status = switch (tag) {
                            case "failure" -> "Failed";
                            case "error" -> "Error";
                            default -> "Skipped";
                        };
                        for (int n = 0; n < nodes.getLength(); n++) {
                            Element node = (Element) nodes.item(n);
                            detail.add(node.getAttribute("message") + "\n" + node.getTextContent());
                        }
                        break;
                    }
                    for (String tag : List.of("system-out", "system-err")) {
                        var nodes = test.getElementsByTagName(tag);
                        for (int n = 0; n < nodes.getLength(); n++) {
                            detail.add("Captured " + tag + ":\n" + nodes.item(n).getTextContent());
                        }
                    }
                    String name = test.getAttribute("classname");
                    if (!name.isEmpty()) name += "::";
                    name += test.getAttribute("name");
                    String duration = test.getAttribute("time");
                    results.add(new Result(status, name, duration.isEmpty() ? "0" : duration, detail.toString()));
                }
            }
        }
        Map<String, Long> counts = new LinkedHashMap<>();
        for (String status : List.of("Passed", "Failed", "Skipped", "Error")) {
            counts.put(status, results.stream().filter(r -> r.status().equals(status)).count());
        }
        suites.setAttribute("tests", Integer.toString(results.size()));
        suites.setAttribute("failures", counts.get("Failed").toString());
        suites.setAttribute("errors", counts.get("Error").toString());
        suites.setAttribute("skipped", counts.get("Skipped").toString());
        var transformer = TransformerFactory.newInstance().newTransformer();
        transformer.setOutputProperty(OutputKeys.ENCODING, "UTF-8");
        transformer.transform(new DOMSource(document), new StreamResult(Path.of(args[2]).toFile()));
        if (results.isEmpty()) warnings.add("No test cases were found in Maven Surefire reports.");
        if (!args[5].equals("0")) warnings.add("Maven exited with code " + args[5] + ".");
        double elapsed = Math.max(0, System.currentTimeMillis() / 1000.0 - Long.parseLong(args[4]));
        StringBuilder out = new StringBuilder(HEADER);
        out.append("<p>Report generated on ").append(escape(ZonedDateTime.now().toString()))
                .append(" by Maven Surefire (pytest-html style).</p><h2>Environment</h2><table id=\"environment\">");
        String[][] environment = {{"Project", project.getFileName().toString()},
                {"Platform", environmentValue(testEnvironment, "os.name") + " / "
                        + environmentValue(testEnvironment, "os.version")},
                {"Java", environmentValue(testEnvironment, "java.version")},
                {"Environment source", "Surefire XML (test JVM)"},
                {"CI", System.getenv().getOrDefault("CI", "")}};
        System.out.println(">>> Test environment (Surefire XML): Platform=" + environment[1][1]
                + ", Java=" + environment[2][1]);
        for (String[] entry : environment) {
            out.append("<tr><td>").append(escape(entry[0])).append("</td><td>")
                    .append(escape(entry[1])).append("</td></tr>");
        }
        out.append("</table><h2>Summary</h2><p>").append(results.size()).append(" tests ran in ")
                .append(String.format(Locale.ROOT, "%.2f", elapsed)).append(" seconds.</p>")
                .append("<p>(Un)check the boxes to filter the results.</p><div class=\"controls\">");
        counts.forEach((status, count) -> out.append("<label class=\"").append(status.toLowerCase(Locale.ROOT))
                .append("\"><input type=\"checkbox\" data-status=\"").append(status).append("\" checked>")
                .append(count).append(' ').append(status.toLowerCase(Locale.ROOT)).append("</label> "));
        out.append("</div>");
        warnings.forEach(w -> out.append("<p class=\"warning\">").append(escape(w)).append("</p>"));
        out.append("<h2>Results</h2><p><button id=\"show-all\">Show all details</button> / <button id=\"hide-all\">Hide all details</button></p>")
                .append("<table id=\"results-table\"><thead><tr><th>Result</th><th>Test</th><th>Duration</th><th>Links</th></tr></thead>");
        for (Result r : results) {
            out.append("<tbody data-status=\"").append(r.status()).append("\"><tr class=\"result-row\"><td class=\"col-result ")
                    .append(r.status().toLowerCase(Locale.ROOT)).append("\">").append(r.status()).append("</td><td>")
                    .append(escape(r.name())).append("</td><td class=\"col-duration\">").append(escape(r.duration()))
                    .append(" s</td><td></td></tr>");
            if (!r.detail().isEmpty()) {
                out.append("<tr class=\"detail-row\"><td colspan=\"4\"><details")
                        .append(r.status().equals("Failed") || r.status().equals("Error") ? " open" : "")
                        .append("><summary>Details</summary><pre>").append(escape(r.detail()))
                        .append("</pre></details></td></tr>");
            }
            out.append("</tbody>");
        }
        out.append("</table><h2>Test Log</h2><details><summary>Maven output (last 200 lines)</summary><pre>");
        var lines = Files.readAllLines(Path.of(args[3]), StandardCharsets.UTF_8);
        out.append(escape(String.join("\n", lines.subList(Math.max(0, lines.size() - 200), lines.size()))));
        out.append(FOOTER);
        Files.writeString(Path.of(args[1]), out, StandardCharsets.UTF_8);
    }

    static String environmentValue(Map<String, Set<String>> environment, String key) {
        var values = environment.get(key);
        return values.isEmpty() ? "Not recorded in Surefire XML" : String.join(", ", values);
    }

    static final String HEADER = """
<!DOCTYPE html>
<html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>unit_test_report.html</title><style>
body{font-family:Helvetica,Arial,sans-serif;font-size:12px;min-width:800px;color:#999}
h1{font-size:24px;color:#000}h2{font-size:16px;color:#000}p{color:#000}a{color:#999}
table{border-collapse:collapse}#environment td{padding:5px;border:1px solid #e6e6e6}
#environment tr:nth-child(odd){background:#f6f6f6}.passed{color:green}.failed,.error{color:red}
.skipped{color:orange}#results-table{border:1px solid #e6e6e6;color:#999;font-size:12px;width:100%}
#results-table th,#results-table td{padding:5px;border:1px solid #e6e6e6;text-align:left}
#results-table th{cursor:pointer}#results-table th:hover{color:#000}
.result-row:hover{background:#f6f6f6}.col-result{width:110px}.col-duration{width:100px}
pre{background:#e6e6e6;color:#000;font-family:monospace;white-space:pre-wrap;overflow-wrap:anywhere;padding:10px;max-height:400px;overflow:auto}
.controls{margin:12px 0}button{background:none;border:0;color:#999;text-decoration:underline;cursor:pointer}
.warning{color:red}details summary{cursor:pointer}.detail-row[hidden],.result-row[hidden]{display:none}
</style></head><body><h1>unit_test_report.html</h1>
        """;
    static final String FOOTER = """
</pre></details><script>
const table=document.getElementById('results-table');
document.querySelectorAll('input[data-status]').forEach(box=>box.addEventListener('change',()=>{
  table.querySelectorAll('tbody').forEach(body=>{if(body.dataset.status===box.dataset.status)body.hidden=!box.checked;});
}));
for(const [id,open] of [['show-all',true],['hide-all',false]]){
  document.getElementById(id).addEventListener('click',()=>table.querySelectorAll('details').forEach(d=>d.open=open));
}
table.querySelectorAll('th').forEach((th,index)=>th.addEventListener('click',()=>{
  const direction=th.dataset.direction==='asc'?-1:1;
  th.dataset.direction=direction===1?'asc':'desc';
  [...table.tBodies].sort((a,b)=>{
    const x=a.rows[0].cells[index].textContent,y=b.rows[0].cells[index].textContent;
    return direction*(index===2?parseFloat(x)-parseFloat(y):x.localeCompare(y));
  }).forEach(body=>table.appendChild(body));
}));
</script></body></html>
        """;
}
