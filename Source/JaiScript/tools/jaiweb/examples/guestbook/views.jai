// The guestbook's pages: plain functions that return HTML. Every value from a visitor goes through h(), which
// escapes it; the page text around it is written as it is.

string h(var value) { return html_escape(to_string(value)); }

string when(int unix_seconds) { return time::date_from_unix(unix_seconds).format("${year}-${month:02}-${day:02} ${military_hours:02}:${minutes:02}"); }

string layout(string title, string body) {
	return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${h(title)}</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<main>
<header><a href="/">Guestbook</a></header>
${body}
</main>
</body>
</html>
`;
}

string entry_card(map<string, var> entry) {
	return `<article>
<p class="message">${h(entry["message"])}</p>
<p class="byline"><a href="/entries/${entry["id"]}">${h(entry["name"])}, ${when(entry["created"])}</a></p>
</article>
`;
}

string home_page(array<map<string, var>> entries, string name, string flash, int views) {
	string cards = "";
	for (auto& entry : entries) { cards += entry_card(entry); }
	if (entries.size() == 0) { cards = "<p class=\"empty\">No one has signed yet.</p>"; }
	string notice = flash == "" ? "" : `<p class="flash">${h(flash)}</p>`;
	return layout("Guestbook", `${notice}
<form method="post" action="/sign">
<label>Name <input name="name" value="${h(name)}" maxlength="80" required></label>
<label>Message <textarea name="message" rows="3" maxlength="2000" required></textarea></label>
<button>Sign</button>
</form>
${cards}
<footer>${views} page views since the server started</footer>
`);
}

string entry_page(map<string, var> entry) {
	return layout(entry["name"] + " signed the guestbook", entry_card(entry) + "<p><a href=\"/\">Back</a></p>\n");
}
