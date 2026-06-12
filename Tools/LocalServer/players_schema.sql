-- Lobby server schema (see Source/Game/NetworkLayer/accountActions.cpp for the queries).
-- verified defaults TRUE so local accounts skip email validation; a production deploy
-- should default FALSE and provide ServerConfig/smtp.config for validation mail.
CREATE TABLE IF NOT EXISTS players (
	id BIGSERIAL PRIMARY KEY,
	email TEXT UNIQUE NOT NULL,
	handle TEXT UNIQUE NOT NULL,
	passhash TEXT NOT NULL,
	passsalt TEXT NOT NULL,
	passiterations INTEGER NOT NULL DEFAULT 12,
	verified BOOLEAN NOT NULL DEFAULT TRUE,
	state TEXT NOT NULL DEFAULT '',
	serverstate TEXT NOT NULL DEFAULT ''
);
