// Workbench File panel — hot-reloads: edit this file while the editor runs and it rebuilds.
//
// Capture contract (pinned by "Lambda Capture Probes" in JaiScript tests): globals like
// `panel` resolve at CALL time and are re-bound per panel-script execution, so deferred
// closures must never read them. Capture the build hook's locals instead, pinning each closure to
// THIS panel: [&p] shares the panel C++ hands the hook by reference (a copy capture would copy
// the panel), and [field] copies a handle, so it names the same text field.

panel.build = [](var p) {
	var root = p.root();
	var field = p.textField(root, "fileName", 0.0, 0.0, 220.0, p.fieldHeight(), "Scenes/map.scene");

	var buttonRow = p.fieldHeight() + p.pad() * 2.0;
	var loadButton = p.button(root, "load", 0.0, buttonRow, 64.0, p.rowHeight(), "Load");
	loadButton.onAccept.connect("go", [&p, field](var clicked) {
		p.loadScene(field.text());
	});

	var saveButton = p.button(root, "save", 70.0, buttonRow, 64.0, p.rowHeight(), "Save");
	saveButton.onAccept.connect("go", [&p, field](var clicked) {
		p.saveScene(field.text());
	});

	p.reportHeight(p.fieldHeight() + p.rowHeight() + p.pad() * 3.0);
};
