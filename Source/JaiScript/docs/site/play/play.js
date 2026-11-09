// The Play page: one terminal, a game picker, Play and Stop
"use strict";

(() => {
	const GAMES = {
		gloom: { name: "GLOOM", cols: 120, rows: 45,
			keys: "W S move, A D strafe, arrows turn, space fires, E uses, shift runs, 1 2 3 weapons, M map, esc quits" },
		grubwell: { name: "GRUBWELL", cols: 135, rows: 40,
			keys: "W S step, A D turn, Q E sidestep, Z skill, G pick up, I pack, tab descends, ? all keys, esc pauses" },
		demoreel: { name: "the demo reel", cols: 100, rows: 40, keys: "esc quits" },
	};
	const term = document.getElementById("play-term");
	const cover = document.getElementById("play-cover");
	const run = document.getElementById("play-run");
	const stop = document.getElementById("play-stop");
	const keys = document.getElementById("play-keys");
	const note = document.getElementById("play-note");
	const status = document.getElementById("play-status");
	let game = "gloom";
	let session = null;

	function showCover(text) {
		run.textContent = "Play " + GAMES[game].name;
		note.textContent = text || "";
		cover.hidden = false;
	}

	function pick(id) {
		if (session) { session.stop(); }
		game = id;
		for (const button of document.querySelectorAll(".play-pick")) {
			button.setAttribute("aria-pressed", button.dataset.game === id ? "true" : "false");
		}
		keys.textContent = GAMES[id].keys;
		term.replaceChildren();
		session = new JaiPlay.Session(term, {
			cols: GAMES[id].cols, rows: GAMES[id].rows,
			onStatus: (text) => { status.textContent = text; },
			onExit: (code) => showCover(code === 0 ? "" : "The game ended with exit code " + code + "."),
		});
		showCover();
	}

	for (const button of document.querySelectorAll(".play-pick")) {
		button.addEventListener("click", () => pick(button.dataset.game));
	}
	run.addEventListener("click", () => {
		cover.hidden = true;
		session.start({ game: "games/" + game + ".json" });
	});
	stop.addEventListener("click", () => {
		session.stop();
		showCover("Stopped.");
	});
	pick(game);
})();
