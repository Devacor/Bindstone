--
-- PostgreSQL database dump
--

-- Dumped from database version 9.5.15
-- Dumped by pg_dump version 10.12

-- Started on 2020-02-27 20:36:33

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;


CREATE SCHEMA "public";


ALTER SCHEMA "public" OWNER TO "m2tm";

--
-- TOC entry 3140 (class 0 OID 0)
-- Dependencies: 8
-- Name: SCHEMA "public"; Type: COMMENT; Schema: -; Owner: m2tm
--

COMMENT ON SCHEMA "public" IS 'standard public schema';


--
-- TOC entry 1 (class 3079 OID 13276)
-- Name: plpgsql; Type: EXTENSION; Schema: -; Owner: 
--

CREATE EXTENSION IF NOT EXISTS "plpgsql" WITH SCHEMA "pg_catalog";


--
-- TOC entry 3142 (class 0 OID 0)
-- Dependencies: 1
-- Name: EXTENSION "plpgsql"; Type: COMMENT; Schema: -; Owner: 
--

COMMENT ON EXTENSION "plpgsql" IS 'PL/pgSQL procedural language';


--
-- TOC entry 2 (class 3079 OID 16414)
-- Name: citext; Type: EXTENSION; Schema: -; Owner: 
--

CREATE EXTENSION IF NOT EXISTS "citext" WITH SCHEMA "public";


--
-- TOC entry 3143 (class 0 OID 0)
-- Dependencies: 2
-- Name: EXTENSION "citext"; Type: COMMENT; Schema: -; Owner: 
--

COMMENT ON EXTENSION "citext" IS 'data type for case-insensitive character strings';


SET default_tablespace = '';

SET default_with_oids = false;

--
-- TOC entry 185 (class 1259 OID 16550)
-- Name: instances; Type: TABLE; Schema: public; Owner: m2tm
--

CREATE TABLE "public"."instances" (
    "id" integer NOT NULL,
    "available" boolean DEFAULT false,
    "host" "text" DEFAULT ''::"text",
    "port" integer DEFAULT 0,
    "playerleft" integer DEFAULT 0,
    "playerright" integer DEFAULT 0,
    "lastupdate" timestamp without time zone DEFAULT "timezone"('utc'::"text", "now"()),
    "result" "json"
);


ALTER TABLE "public"."instances" OWNER TO "m2tm";

--
-- TOC entry 184 (class 1259 OID 16548)
-- Name: instances_id_seq; Type: SEQUENCE; Schema: public; Owner: m2tm
--

CREATE SEQUENCE "public"."instances_id_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE "public"."instances_id_seq" OWNER TO "m2tm";

--
-- TOC entry 3144 (class 0 OID 0)
-- Dependencies: 184
-- Name: instances_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: m2tm
--

ALTER SEQUENCE "public"."instances_id_seq" OWNED BY "public"."instances"."id";


--
-- TOC entry 183 (class 1259 OID 16525)
-- Name: players; Type: TABLE; Schema: public; Owner: m2tm
--

CREATE TABLE "public"."players" (
    "id" bigint NOT NULL,
    "verified" boolean DEFAULT false,
    "email" "public"."citext",
    "handle" "public"."citext",
    "session" "text" DEFAULT ''::"text",
    "passhash" "text" DEFAULT ''::"text",
    "passsalt" "text" DEFAULT ''::"text",
    "created" timestamp without time zone DEFAULT "timezone"('utc'::"text", "now"()),
    "lastlogin" timestamp without time zone DEFAULT "timezone"('utc'::"text", "now"()),
    "state" "jsonb",
    "passiterations" integer DEFAULT 10,
    "serverstate" "json"
);


ALTER TABLE "public"."players" OWNER TO "m2tm";

--
-- TOC entry 182 (class 1259 OID 16523)
-- Name: players_id_seq; Type: SEQUENCE; Schema: public; Owner: m2tm
--

CREATE SEQUENCE "public"."players_id_seq"
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER TABLE "public"."players_id_seq" OWNER TO "m2tm";

--
-- TOC entry 3145 (class 0 OID 0)
-- Dependencies: 182
-- Name: players_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: m2tm
--

ALTER SEQUENCE "public"."players_id_seq" OWNED BY "public"."players"."id";


--
-- TOC entry 186 (class 1259 OID 16592)
-- Name: unlockables; Type: TABLE; Schema: public; Owner: m2tm
--

CREATE TABLE "public"."unlockables" (
    "code" character varying(24) NOT NULL,
    "player_id" bigint,
    "promo_type" integer NOT NULL
);


ALTER TABLE "public"."unlockables" OWNER TO "m2tm";

--
-- TOC entry 2997 (class 2604 OID 16553)
-- Name: instances id; Type: DEFAULT; Schema: public; Owner: m2tm
--

ALTER TABLE ONLY "public"."instances" ALTER COLUMN "id" SET DEFAULT "nextval"('"public"."instances_id_seq"'::"regclass");


--
-- TOC entry 2996 (class 2604 OID 16576)
-- Name: players id; Type: DEFAULT; Schema: public; Owner: m2tm
--

ALTER TABLE ONLY "public"."players" ALTER COLUMN "id" SET DEFAULT "nextval"('"public"."players_id_seq"'::"regclass");


--
-- TOC entry 3132 (class 0 OID 16550)
-- Dependencies: 185
-- Data for Name: instances; Type: TABLE DATA; Schema: public; Owner: m2tm
--



--
-- TOC entry 3130 (class 0 OID 16525)
-- Dependencies: 183
-- Data for Name: players; Type: TABLE DATA; Schema: public; Owner: m2tm
--

INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (1, true, 'maxmike@gmail.com', 'M2tM', '', 'c9c6490c69ddacdcc633e66c518369683eb4d7938e7ce11a2369ec8e580cda4639797a66bd072e2a3c7466f9ee35a1693ff3aea36a20e4120491b4bfa8edd66e', 'gfCZwqocuyFRsFM06lis1WQdZeFVHC4j', '2018-03-07 11:14:51.283298', '2018-03-07 11:14:51.283298', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (2, true, 'jackaldurante@gmail.com', 'Jai', '', '8421e985ecfa2a2ca41cf258a12b16e06f4d6e4caf4c23ca7f68b886bf31927810e2f66218ed66cfa26ab51610ae81a8409139c738a205835be300f9ac977c87', 'TE4hoCswEAd2aP06e2EkfjcHhCyB6wHo', '2018-03-07 12:04:04.507902', '2018-03-07 12:04:04.507902', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (3, true, 'lostanfoundit@gmail.com', 'MartyMcfly', '', '487a0ee5a9287242b315b3e061e35c029258fdf279215234adb052ddf7ec97e93c7359a367d2e2c1db60dad4298f20ec6670315a53a70b3355b4ce2e21c4e358', 'rvMp1ixD-peJ5bk-C9o_HuwdO5dfGNNU', '2018-03-07 15:09:22.511578', '2018-03-07 15:09:22.511578', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (4, false, 'Centrix@live.ca', 'Cenny', '', '6452e136934370161a38d30bda68ada0e694162238f79d30dd8d60e78f9e29a086da510939bfa9c76d5f067f98c419688fa5500c2d487b229de67732241e46d9', 'wUiYusE7iJxFXynB37OlI8E_IFqqY-ay', '2018-03-09 10:34:18.641235', '2018-03-09 10:34:18.641235', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (22, true, 'mrichmond97@hotmail.com', 'B4RK0DESYNTHEX', '', '7fa0ececac5073fa587ebb2bd08eb83eef68f1c796880b527312dccbcd8f115e2704480692b7422570c9ea7a6cf6c0d215ff25d6db414e531201528a45bd969a', 'f-Eur-PqmHyWVTsEQcac5DRHTnHQ0-BZ', '2018-03-10 07:51:01.307197', '2018-03-10 07:51:01.307197', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (5, true, 'trevkellogg@gmail.com', 'trevkellogg', '', 'c1dfc6c7fc61c7fe6790f2c5c64a4bd870e178c4971c200480c3235484d458599fb78fc7b1f0b8398ce135700c06f82d23f77fbc0b4de9fa02424135cc0eaf09', 'MaLA9s2dT8Bfvu_CqNmhM6m2OAsF60IO', '2018-03-09 19:13:06.521044', '2018-03-09 19:13:06.521044', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (6, false, 'alexandernaraghi@gmail.com', 'juiceqq', '', '4b1fa451980652c5b2c915d28d6fefa37c4f8a1d0e9aa77c35644e011240acd1e418883a316520652e3a8f7ca7d623bf265f90d95b89e91d68c3d87ef1da824d', 'KiIm5WMAf4FvdWD-P4EDenqFfHoDd2Ji', '2018-03-09 19:19:07.17951', '2018-03-09 19:19:07.17951', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (8, true, 'coronamonish@gmail.com', 'MoSenko', '', 'bee37254550decbb408cd70fbd23562fffcf21836fa08f2c57a5fb1bfd65cbe1c026311149c263ff8da3b8c07ca473d57a89552941e96f54c25263a90816a78c', 'nDYmYG1R6FyNLwImTQuvfy2eeJErjkqP', '2018-03-09 19:25:53.752316', '2018-03-09 19:25:53.752316', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (10, false, 'gabriel.purchases@gmail.com', 'AlaLobsters', '', '116581bce8720a6c6d6daf6f3b7bcc29b38c0fb617f041c4b2dbdd8dd3baba441201853c2a4b01df27eca52230718a42a0983d3c95b2fc4b62e3f3c6be136132', 'XcZFMp_e6JjFGn55WtteER-ibljVbJHX', '2018-03-09 19:46:46.198539', '2018-03-09 19:46:46.198539', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (11, false, 'Kishowolf@yahoo.com', 'Kishowolf', '', 'ef4dffa62460f9269cd9331a5f18955d7afb7e37f79f6d656f99901fdb961fc2ce5a127e26f0c333ad08401d5660cda35c2ded6a0defa05b36687ff3c2c64254', 's8HQNqy2GPOwcStwuybkqnKZfu22IXT7', '2018-03-09 19:48:28.299669', '2018-03-09 19:48:28.299669', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (12, false, 'k.burkett@gmail.com', 'Novad0g', '', '171f63c552a550b7e4bcd11774a2edaa86e80d90446239cbf30c822196a89da5d4dcf1b34ffbf91d15a19b6ef736539ac542226c78d7a3e84176ac41432b3593', 'xtDi84ne8-q_MDYtbMjGqqzp_UvhY0zb', '2018-03-09 19:53:15.820308', '2018-03-09 19:53:15.820308', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (15, false, 'adrian.hedger@gmail.com', 'Rook', '', '45c9a79c530fa6e811fef6962892c1f72eeec70539bcb08274e3de7d55d40026620669383af13dcaa742d3e384ea7bc25d2e46beb3cd428c37658b5cd3dd86e2', 'gJJ9oOWfRwjG2n73asUi26mdW2rtCgxj', '2018-03-09 20:04:55.727468', '2018-03-09 20:04:55.727468', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (14, true, 'ianwilliams633@gmail.com', 'iwilliams', '', '1d9fd3c025a6c943a0799a48793358be5a6840a7b3472386255b9ddff73c83eeb1fb72bdfe4e3895f8795ba605e696489863f0faee134c849b856ce5060b0683', '1kJ0wg88RQ2BXP8o0JThLA33eLg3vLvG', '2018-03-09 20:01:34.487889', '2018-03-09 20:01:34.487889', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (9, true, 'jlstri@hotmail.com', 'kinoko', '', 'b9d4e958120387f5d47ed1ff9e1f550cde46144b850e84c718774f31257f17329c4788f3072b2e35fe003074eba7258c00a6da60a48cd35a02898eeb4c97cad0', 'LcHuZa5lpEcqoXuqLaBuuVr4sDqtoOOC', '2018-03-09 19:27:20.233998', '2018-03-09 19:27:20.233998', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (13, true, 'pandaroohoo900@yahoo.com', 'pandaroohoo', '', '5fabf1724be210e6fe66d6aacbe7ac1cf67364f7c4082d013ddec165943b75d06ec0e19ddb3b354e7f83c968336b3720de8287e473acbf066acd8b88905f60c3', 'c2AXCjCeM-EOW7t3dSrToqFYhBSVdFA6', '2018-03-09 19:56:30.693734', '2018-03-09 19:56:30.693734', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (7, true, 'doughty@sleepypenguin.net', 'ASleepyPenguin', '', '6de13bd8803f00febd962afccb7940447617a990d3f34bcb191d5534e7c508d9652aa8cedcfa0fb9118f338932fdb75b1dc632e852b9703116a6802f5d5566a1', '0N-QTmz8rfEZ51eN1d9BswNXoM0KpCz5', '2018-03-09 19:23:01.846459', '2018-03-09 19:23:01.846459', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (16, true, 'captnelle@gmail.com', 'HauntedJynx', '', 'e06eb41b23d1ba5fba21a8517851eae63163dce9b4d8d70bad853754709b72cd4a9ee5c5ab02e75afd9f986d6f8637080d16acaff60b53f7c72040eac90b4d56', 'U-jgkv6VYcIIjA0ctxhUR9NmwUoBTsTS', '2018-03-09 22:56:30.601664', '2018-03-09 22:56:30.601664', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (23, false, 'kgllewellyn@huskynet.ca', 'Kgllewellyn', '', '64b2cc9d83d604f257df90cde10f1b10fb56dbee2d27423cf6683bdfa8911fbc3b9b62835403e0a93ae6a34d8cabaa8b32789ad341a7811fd5873ceaaf4608d8', 'HzSYz3xTS3yGx97CzhcQjCNWceb1tP94', '2018-03-10 20:04:58.151736', '2018-03-10 20:04:58.151736', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (17, true, 'TaeliouWolf@gmail.com', 'TaeliouWolf', '', '29462723121b4bd6d61fcd44a576564ecfd2a98d8b8407a09035816e997c1d9a3bffd15d6bf5ed2fba021bf53dee1eed50d764b83b66e59617696d019fbb2779', 'sHf888wIV-JVMzIxcTJiHD3I4RRis5Hp', '2018-03-09 22:59:12.595295', '2018-03-09 22:59:12.595295', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (19, false, 'Zealths@hotmail.com', 'SimplyKitten', '', '55153faac1db355c405701e37bc703cb58572fc3439e1cba156eb1e4b22ce8bd331a65c368b3d319c47d7885bc950053f0e90ea8a950d8640b23ec2f93347c3d', '6al2nm4a4n-Itwl2Lp2I791oQInOZ9jw', '2018-03-10 02:51:41.662041', '2018-03-10 02:51:41.662041', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (20, false, 'Torwin@hotmail.ca', 'Torwin', '', 'e7c587c8494ec13e012500eefe77ec387600af8fc9bdbfa92fe91d44d73b52524b87455d1237032337415aefeef7547a4a080603c83a7198ef63ee983221ddf5', 'nrO7xLoIQ2eu2euKQPlMkEbEMD3vAv9t', '2018-03-10 05:44:53.184152', '2018-03-10 05:44:53.184152', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (21, false, 'liamstevenson@techie.com', 'JohnDangle', '', 'e6f8e60e947355d949ba4fd21d48a4f57ddb7d169d2a89d7f38cf947a60ba047a81c47f98d6144b4fa2e5fe459613046d8052817d1ef4b5c17478c837fbbcb47', 'f4NgTIH0u5ocKB1XGxxgv70DlP9Da48J', '2018-03-10 05:52:40.433925', '2018-03-10 05:52:40.433925', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (24, false, 'azimuth.miridian@dragonwolf.io', 'Azimuth64', '', '815fcc623b25c2cc5d7ec75ec4286ac44d6e529a39c9c0cffa9ae5d238bd1cf1d618ca4c659a8b0f36d5247ac4dac1365835706d8de7b877eb81f6122009037f', 'gl_bAjXIOKkeYcEeYefFn8y1EaXNqExR', '2018-03-10 20:05:20.803962', '2018-03-10 20:05:20.803962', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (26, true, 'zyfnyx@gmail.com', 'Ferxof', '', '1f4b02fb24d62856dd9c9900f7a636db2ee43d039a7b1cb97c76bd880e25578567f0116c2b2e2d1a6a7f80ef1ef0029ba69403e9b4b3dc6b9c2d4e13dc5b4d73', 'VMdNOq6afw3v5-w29OrS1t-ockeuA-v5', '2018-03-11 01:24:24.128076', '2018-03-11 01:24:24.128076', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (18, true, 'colinohreally@gmail.com', 'ColinOhReally', '', '00e22d0396ed0646fb208704b8833c1d96763ca00a823a449d62d2f945eee417b14f30040da8d5de71c05350784580053bf4c52794500047736ceb8cd5a8fff5', '4eByWSk-D-yLnFGoydNVilPrS6c0GYvo', '2018-03-09 23:07:39.684555', '2018-03-09 23:07:39.684555', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (25, true, 'makinator51@gmail.com', 'LukeOhara', '', '4bf39a85c595fa08c6b56963a83519dc9c06ab8b32c4cdde28cb1cc1f317ea2171de04aa691ed1587cf5d9863db3d7ffd322eda7845f15c7bc19aac76fc01e1f', 'GoJVMf2elHlUw6oHtvjQdp_LXzVEB2_L', '2018-03-11 00:05:21.949374', '2018-03-11 00:05:21.949374', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (27, true, 'kithop@gmail.com', 'Kithop', '', '7156c35792918e9b05bc210a2a87d577ab267f58401ee849da305e17e6d9825061cbb35da6ae4fe253a048e7b07a466e40ebd843ed18c983a998350a6d82be49', 'h0Mds8OxRN8yxAn4OZmIl4949NQrEOux', '2018-03-11 20:18:10.257942', '2018-03-11 20:18:10.257942', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (28, true, 'Jamesbear@gmx.us', 'Jamesbear', '', 'd5fd417b2c38a9702b07a87b5dbad2fe5e60ec61850d8bdaeee10652cb9457abee35524e16098895dc14cbe75c4cc2a1f9477994b6d374ca2412e145dfe7a01e', 'mysOQvQKzXYUFbkQVcTHqS2u3ZJ22KpQ', '2018-03-12 00:56:58.508205', '2018-03-12 00:56:58.508205', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (29, false, 'shadehunter@yahoo.ca', 'SimonFeralle', '', '0468bf6420628c93e7704210d1fa8992f255becf9ca0cc577bfde4439ada974b856754d5916d9ef826120661d4ad0e8a83ed5dfb1ab5bee47d07a4e8235ce4de', 'jOu_D4dFfv7qhLmsHPeDlO6RlDnE1yip', '2018-03-12 07:38:29.435335', '2018-03-12 07:38:29.435335', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (30, true, 'alain_carter89@hotmail.com', 'Korozar', '', '4a803656f93e9e5ba730102df1aa61e16e1ed5f7892aded60bdce1ddf1c33af707a002c60e4c37ce40306f4917b68795fd67689cdf029b8480a01ab0d33da8b0', '1IHI7NHsf3-FVG3s0gVO6l9k8Np5w4wF', '2018-03-13 02:52:57.792166', '2018-03-13 02:52:57.792166', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (31, true, 'naruto6182@gmail.com', 'Tails', '', 'ec3673d7730008dd88a76e04eae2ca6ec707652646d5ab4cf457bb2c08dd822f25180e2a9cf0c8ad4fc339f7a6062a38c0facf89d925470c865aa8ae8e44a5c2', 'VA1bgCgO_x2Dtw0GUTy83zvdcaIAt4Gg', '2018-03-13 12:17:46.9317', '2018-03-13 12:17:46.9317', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (33, true, 'fishybiscuits.ig@gmail.com', 'FishyBiscuits', '', '8cdfeba9e8939e5f3e20917c915783933bc84c0b73d087f84df1df7a0338ad12ea33e94efebfbe59c70a457bfb3aba1dc968d446328ab059ba3bd94a8d9d7ae7', 'Bq4Bg8i7kW95Fi0njO_1y1NNWC4HMZkD', '2018-03-14 05:07:24.957946', '2018-03-14 05:07:24.957946', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (32, true, 'icefoxx@korrentcity.com', 'icefoxx', '', 'b0bf44a2492ab66bf2efb3d271e475b09c18c4c567ce8015f9f34b5c2bc2e8031c24e98828390baef9c0c9970e7ebbbde98c7a798e6e15dca8900984a4ebd012', 'ZmG8ucrRUmVh6y0oyOaLrgSFvMyvHT_Z', '2018-03-13 19:10:20.451273', '2018-03-13 19:10:20.451273', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (35, true, 'arlaxwolf@gmail.com', 'Shard588', '', 'fcb4672d07bc70f236f79031b164b0441b488001d4b046f01980db563c069fea2e35a10534ca7831567c9ecb61bc64503abd18f1073db6a20eb9db8f694baedd', 'aoy1Ej2j_Uqzg8oLd1LVLYkO3oReCFRL', '2018-03-15 05:27:47.398597', '2018-03-15 05:27:47.398597', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (34, true, 'toxxicace@gmail.com', 'ToxxicAce', '', '345b15ed6a05368f29226e75a63daa7684f86b32e0a2104fa157d33970283de11d70c807be6983adb5796214ed976e7ce9a6cc3d20c437ec7d4ba5ae967b6486', 'O8-PsPQeMPfSB5caJgh7SzNoaKT5UdLf', '2018-03-14 23:13:25.518389', '2018-03-14 23:13:25.518389', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (36, true, 'Camcamcam689@gmail.com', 'Bukakkarot', '', '6dfba16fc8bd2b890ba44cda2e98607b5c75b0dc3d9dec150014fcbb062dc9a6d4a36b61b000e785d3ce687665dfd7d6973b6093bc3f13353e7654fc77eec512', 'XvZ9YfCreU9WrACmu_PLiDKo-5z-E9ek', '2018-03-15 16:39:23.424977', '2018-03-15 16:39:23.424977', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (38, true, 'edurantejr@yahoo.com', 'LCplTweak', '', 'e179677cae9ab024af9cfe478af220a3b1aec33460d92959ed3f5c07c6e020c4828f849ebf9259969d96bdee2f489b7cf69890932ad1be404eb8371385d5dd86', '3vWY_RfMnGZWkDoQqvLINTExxxzKyGEu', '2018-03-15 20:36:25.534275', '2018-03-15 20:36:25.534275', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (39, true, 'dragonmanmike@gmail.com', 'DragonManMike', '', '07433741cf7f630893c0320ae876750a924ee7ba51a7959c93d3375aab884847f83ff981603d6cc9f76ea3caa05cc11b57841543d890a05912a76ec73433b634', '3sz8ME4AxigCP-VeA2n2T8YyE_iCtBxv', '2018-03-15 23:12:48.669433', '2018-03-15 23:12:48.669433', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (41, true, 'mrsteventhamilton@gmail.com', 'Vexx', '', '2d950c86c72c5d0cd72e5bbe1fa973257e289517e04f3542cd127c2d2791357c26dc5b1284d9aadab759806e291dd887495894a4baeaeeb594f46bd744325f06', 'YyUshWafvwS4rrSTzT96AKjWu_-UXkoe', '2018-03-16 03:01:25.408049', '2018-03-16 03:01:25.408049', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (42, false, 'dragonwarriar678@gmail.com', 'Hydraconic', '', '9871c2ddeedfd2b28b96ba1c0477b278aea1b3fd859c8cbe389d2e1ae4339dac816ee9be760f67c0602324137d6bb0b5015690105799321f6d6e260389e94377', 'RZ6JswHzQF6WP5-pam2EEtV6ROXhKE30', '2018-03-16 20:11:24.852905', '2018-03-16 20:11:24.852905', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (43, false, 'chaserb007@gmail.com', 'ElDonkoHancho', '', '46bbc9d854505aad49fb4e3d07de03be7edd9b1d13eae5d59f5f3fa55a10fcb25ceaa648ab42a0a0101e76027fce8607f280202e9ca6630f4a0c094e15945168', '7mrGka_n6rsHJZrEnBsMhkzvRBAj1k9L', '2018-03-16 20:35:21.30916', '2018-03-16 20:35:21.30916', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (44, true, 'arabidllama@gmail.com', 'AshCat', '', '3abf80fb63a8d8343594b965796222f6740ff5862afec756b7d48255524b8c2e66f99c027f714d0003ef60625533d5e32a4dcf5b8e869f49c271d59f7a9a53fd', 'edbNddwqcpH-Kh22JTwzLEiztqyOS2qs', '2018-03-17 03:04:53.844884', '2018-03-17 03:04:53.844884', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (37, true, 'xxmilichanxx@gmail.com', 'Chipo', '', '11ec6454d1734ff382c52c077045dd0e1066a8f2a41498a4fe72a879ffeed1313a8f865d3dc2ac346c042ac1571676346f279f4e1b389adc919a7d768095cd5e', 'txhK9F5NspLlvqhZUZNru6u-kkPoSbva', '2018-03-15 20:31:55.930339', '2018-03-15 20:31:55.930339', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (45, false, 'naruto6182@yahoo.com', 'Fox', '', '0c32dcad5df5f193d00dcb7100b5ab98e47b69361e862be70026d6b02ec352a3a4ce116a8a641470b1600582aa882f7a0d82e603668afa03d9c08f4dbc78e415', 'KhO61yuJBIkAkk0TTfpzAssx-3ftGA41', '2018-03-18 21:47:56.563885', '2018-03-18 21:47:56.563885', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (46, false, 'alex10dragon@icloud.com', 'Skystorm', '', '6baae2ca2cb431698806d544c5319a6bd1004c2de136b23c642b3947c3eec5b09205b9b83085329d1f9ab5b143fb426602fe21be1b51a6c4b39c75b9ac27d7c2', 'qRfIKmjl_kypcQcUa5l6eBRrG5KKopG0', '2018-03-19 04:52:33.635036', '2018-03-19 04:52:33.635036', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (47, true, 'othermackenzieblake@gmail.com', 'SpookyFox', '', 'f93c3a0fe9394d0acb6dcdc0a9be95b9d9c90e255c973dea670a7d3f01b85d9812fc922277372744885286e703aaf7ce73baf0f96d0cce45199d6d99d7dbf457', 'ob_ynJn9hC__3NpQ_5JGUoGEyPRCCqOF', '2018-03-19 20:44:50.12323', '2018-03-19 20:44:50.12323', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (48, false, 'cjguineau@gmail.com', 'Adeline', '', '805e343d599cc42d740f62e6a1607b791deaf519510222fcf2dc4065aacca6c87d974390a2c36e350d511d50f09877133466cee95e1121c1bfd5df14cca8db76', 'cdEzqD2BUjgW3_RgYCWkxMnW83QSxQzh', '2018-03-31 09:01:01.017571', '2018-03-31 09:01:01.017571', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (49, true, 'themujifox@gmail.com', 'Muji', '', 'c92cf1c40f90837e57a59211cb03bead9b1914e487468040855b65a31fa39636b2139eacfbd04bfd2f041f90f44afbf4d8dafd7405190e1744b6363debc3ae53', 'HLw0CqIBABSVkk7blY09d0BrAUXmHCG2', '2018-04-10 05:47:26.725445', '2018-04-10 05:47:26.725445', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (50, true, 'mrpresidentsmith@gmail.com', 'Iconoclast209', '', '89a2f2a8d5bb31a73e77bdde87cee51295ff62a267e9f514ec8ffc9d3b932c4475e25cd308a9d97c9103377c8129fb1cd698ae5ea6c0b5113f2fd180dfc5ebf3', 'w89GxGlDqf-nkjFaXV78wwEpCknx_yDY', '2018-04-19 23:08:02.424313', '2018-04-19 23:08:02.424313', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (51, true, 'donaardrakeclaw@gmail.com', 'donaar', '', 'e52c1b82a86ea941e956d0bc5b236860ab3563ff17db3da9dc84fdced7731eda4b02a91894f2b142ff44f7fdeacb0881da5a26e2889f1daa0258f2d20ea0223e', 'e5Uyqt6CT7sQH-TkU1DJXCGSa1MFO_it', '2018-05-05 21:41:28.7626', '2018-05-05 21:41:28.7626', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (52, true, 'stripes.the.white.tiger@gmail.com', 'CheesyFeline', '', 'ce826693dc569424aee37d76c7e69c62ccfaaa424fa90c889c3e1d1523f6239b9f008a33d1c747b194b5def8a6ecb25f5bc5b9a483f536781056913bdd9c100a', '0g20YxNI9Ke-QwoJeDUcQ_2MaHAW2j4x', '2018-05-09 00:06:06.518428', '2018-05-09 00:06:06.518428', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (40, true, 'sheldonklong@gmail.com', 'Ometochtli', '', '4e76d39179abc5f738c0442dee845a53ea3914aa3777a05626c051725eb5ae7d7c2663510e2e5ff24e34ec545318a5a4cb4aa694d9f79d8d85764b8a48a79300', '5W79DorzCoIEkWV02c_wX9l-ga3ZHUdb', '2018-03-16 02:08:12.44518', '2018-03-16 02:08:12.44518', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (53, true, 'evan.svendsen@uleth.ca', 'MrD69', '', '08115aab9d53e27d5f490ce068f6287374823595e59cc4aae86707b9a8749b1f2e7b02703e07e135f5acca2718e16ff5901818b996c12dc13c0bf242a0a6e7e6', 'fxydKt62h8PKAQ5N6vcBSJCRKNXlNGK0', '2018-06-04 15:43:21.158366', '2018-06-04 15:43:21.158366', '{}', 12, '{}');
INSERT INTO "public"."players" ("id", "verified", "email", "handle", "session", "passhash", "passsalt", "created", "lastlogin", "state", "passiterations", "serverstate") VALUES (54, true, 'flamiinghost@gmail.com', 'flamiinghost', '', 'e40d152dd31e452e9d92f049bab1ec8b4ff2de1602226428debbc3df307450e732b0eb5224cd06ad28549397c83f874293b07ec5923ed07465b0519f00afbe2b', 'k2Fwlc6U8SH5S_KM9k17wLJmvXrL8WOX', '2018-08-24 19:22:11.097683', '2018-08-24 19:22:11.097683', '{}', 12, '{}');


--
-- TOC entry 3133 (class 0 OID 16592)
-- Dependencies: 186
-- Data for Name: unlockables; Type: TABLE DATA; Schema: public; Owner: m2tm
--

INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8222SELPBMJEY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF822BPHBBYKTYL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF822KY666GC4LM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF822QYS49HJXJF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF823F5Q5PFLXWJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF823GRW5GLC3MV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF824444C8Q9S3D', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82484ZRE7735E', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF824AJYMFAP6RG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8255KS5USLV7H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8256BYFUEEF6Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF825BSRB7HA62V', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF825QQF9DLRE3D', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF827PKGT63JVMM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82934GTTCXQZQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF829DLU6UFKPB3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF829DS9FR7TRLT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF829JVAXY4LDVZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF829VS8LE2HA6K', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82A4G867J4EHQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82BK3LPML8ZK2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82BLLTKBPXFVZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82CEGCJW3QHVJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82CS8KXV43R7Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82CWHKT7UMT4R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82F9HUWYQE9UG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82FVY9QGAEX3T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82G53UW4XHTZ8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82GXCAKDPZLWD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82H3SHVM3ZHA8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82J42MJAP777J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82LWXAQ8LXAMW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82P8SEGZUJKP4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82P8UFBRV4YY3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82PRLJUMWM46X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82Q6F6SJU9BUM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82QY4PSFK3RLL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82RP5DG7QYV27', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82S9K2EK8RDWB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82SBRXETHETHE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82STFZTKWH2TY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82SXPXBUPYGGY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82TP4MGUAZ9JS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82U2GP99LKHJV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82UPGSLDEQCZ9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82VVFATMB6YA5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82VY29G2LB727', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82WQ3HVGG9MXD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82WTE9ABGZE38', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82WZE2M55YJXG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82X8UX7F99ZB7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82XPJYTAW6EQW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82YRWSC4WTFRC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82YS9XXYRBBQH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82YXGHSV44GY7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82YZ9F5YXBWFR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82ZEFKXGYHXFZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82ZFWAPGEFHLZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF832EBUJA5SXM6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF832ZGYWXYKE74', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF832ZPC4ML7SW9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF833HET8U4S92S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF833YW59EJ8UFD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF834A8J8PC9A62', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF834S82DKUSLFJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83635V54CQPZR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF836F72FDWPDQC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83753JU2FYGES', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF838KCUEF95BVR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF839DPL9XWEA8J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF839EU9UTPWLXR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF839M4EU9VMW7M', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF839XHR3D26H4K', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83B8A9XHMGHLV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83BC6U2J6L4Y9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83BM98BXKPRK9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83BP3PLZP2HWK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83BZLE5GCTDSA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83C9XEA4KRFHC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83FU4YX3M5MX8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83FUZ8M5P9EYJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83HY5C7HCG29W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83K56MG76KR2J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83KA9ZKE7BRCK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83L3VMVHTJKXM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83L86ZY8MEVB5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83M6VDU9AE6HD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83M7LQLY5JWD9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83M8QF7ZV34Y8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83MGDGTQYVFLU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83MGL8A76XWRB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83PZJ3UZ9UBMM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83QTGJ3XXEBZB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83R9TA9SU928C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83RRJDKGS8WE9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83RVHS6JLBYG9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83S62235KPAUW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83TPGTEHCMDXP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83UEPC7FF6GX6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83UH7SG93EJJ5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83XPBKMXJT5QK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83ZFF7FTPLLKZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83ZL6UBB3PXDP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF83ZUEKA2F7KC6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8446F2XBGXP4F', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF844DE62FTA85S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF844ELM63QUGBF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF844SSE9YDRXMD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8453H8RPD4BRF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8478ZLMD2P95G', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF847UTQHLU7PVA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8487B5YLZUX9S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8493JKFY4LGMY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84977YHZ7CH9H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8497EPYMVMQ3J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF849TTCP8RU36K', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84A5K4UGQ96UC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84BGE45VPYXQ7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84CDTJ9S683DX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84D8FEK8VD6TA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84E2T9FPXQKLF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84FEADWAWH2DA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84GH5P4C74WS6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84GLD7XTLUG7F', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84JCXT62E38T2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84JV2JQKE5QML', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84K7YL9UVBFDS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84K9UT42JMQSZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84L822FESHTYJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84LKG9JJDCPLU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84LMTZ94QTH6B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84LZHU7DMCTVS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84P4T8LX3TDHK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84PGKDLML2TW8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84PGZYFH4STKX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84QJ8PJCBP8D6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84QMBRV3LY6ED', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84SL4UV7KQ4C5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84SMCQAFVL2V7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84STVC5LJFC3P', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84TSSFBR7DM7T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84UYLX23K4PL6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84V4AM9VHD52Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84W8STUXZQGVW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84WJS7MK6E6FG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84WQSRT72FKGP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84WSLZFKHXVBR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84XZ5KTUGS8AL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84YCXLPVGFUWT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84Z9P3FTV4S3T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84ZMXFLHW2MR3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8523M5WYY4L73', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF852A2C42S5HXK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84JKKAT2XUFWH', 35, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF82KS5X562M53W', 49, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8535HVF45ZHK4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF853MS58XM4SFC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF855VC7M3UCSS8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF856BTLKLC9KMQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF856G8VWU728RQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF856UBTYJ37P4H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF856WWKJ4P5CKL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8577MY883ERXA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8577V4R6XYLCM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF857C2LES5RL56', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8584TDWBRZQG8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF858532R3J8BMQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF858QGFX3T7AS6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85BC4QBXKZ79D', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85D85B8JAGWEP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85DK26ASWMF6W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85E6QFLMBZEQJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85EY5QJ876YZA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85F4WTKD4Q79H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85FDPP9FFPZ8Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85FTTG39BJB88', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85FVF8CSAFZTY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85FZZYS67ULPG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85HVUSBL6ZT89', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85HWR89UHTUEX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85KDAHBE8H873', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85KJFMAMQSX3L', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85KR4M8TSC4RE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85L64MWK46448', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85LM5MFR6H7JE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85LVFGCUSY794', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85LXBDRFR965J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85M3UMPWGCVXD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85MU222QSUZ8W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85MZGDLUXVY7H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85PJ8VGR4KE8X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85PYBJ32R6HPZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85QXXT5CWEG4G', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85SAV83E4QQQ8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85SGDKPV5GADE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85TE5AS9YYM4W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85TFUJGRPJTW4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85TLYF2JDKQQU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85TQUQZZ9T7HA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85TZSQQXRA9UR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85UADJM2VKXVX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85UZ39GGZ7EDJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85W47VWAZHCQF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85XL7DPQPBY5A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85XMHDQF7V6Z4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85XTX6UX6CRKF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85YK8DV6LADWK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85Z4VD5KGTXMB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85ZCQAUFD28VU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF85ZVZMB6C33BT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8637XCSYWPLH7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF863PBKU2BZ9MZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF864JGVHR6S349', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF864M8ZJUAD4XY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF864SG3UD65FSG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8654F2S3YLZME', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF865MCTV3DK2AG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF866JXFS95UA4Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF868RWK7TD8XAG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF868XKWTS4ZJTJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8692DM4Y79RAV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8694E5K7FB9MS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF869LMGUTFD7QK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86A74MFSMZGED', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86BHF7T3UQH4R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86CQ48HRYRGUA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86D5ZTUVZ27VS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86DGSH4KGTHR8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86DS4A4UC2RRR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86EW7Q486PWCE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86G273GBHLG8G', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86GBWT34ZUQ2P', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86GS2RSG5XGS3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86J97AFPQWGKQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86JEXXGRQMGST', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86K4VS7F66DRG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86KTX4GHSR864', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86KYYLQCF59RQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86LK75UDBL7PH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86LS7AAVD6YZK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86MQX7JPAM3BL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86MXQEAWM8XJS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86PJ78PX7489R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86PSLA8MRL4EV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86PVYYYWMKM9M', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86QCPA24RF7B7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86RYM2HRS8KBT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86SATTPS54AQ5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86TAL6WPWPZH4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86UPBL6P49KUK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86UVJYMYJHWU4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86VKUVBC7HLPP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86VLJXLCPL6HJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86W45F7EPUVHY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86W6TLB85RZW3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86XV4GY34DPRY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86Y4BRGBR2R3G', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86YZKEF7FFECC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86ZBXEL86SH6F', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86ZYTFQRVJ5VR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8728YGHRKRL4T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF872VQ5MC5X7S5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF873DBRW9M2FGV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8748TG7SZ3P2S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF874ZYQ5VSX6G9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8759BRCKTZSTS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF875HKL8M3XDUS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87785VCLB4APE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF877GYFGXHL8P7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8789EBXFTTGPC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF878GG5SFEQRFZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF878VEY4QZYQRE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8799EC4ZBU6VS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF879YV6LP3GALT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87AC9CUVLDF6R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87AEL6A32BA35', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87BLEL665QA3H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87BLFKJJ8DB64', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87C9MGQLTFTK6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87CA7VSYJY3C5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87CA8XJLMWLMZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87D5MEZQ975RP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87D7H4LSK8VU3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87EKFSSV7ATY4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87EMFTUXSUM5Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87F2ERLYYW2XM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87FA6FQ9FTSMA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87G7HSFF8WPXC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87GADXGYUEAA5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87GJABB9VV57W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87HHCXZ54WWPX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87HP3JZPF64CP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87J6XDDYW9FM8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87LUDHBBESBSA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87M4828QGRRLA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87M6DLWUY237J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87PAJEL6F55CL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87PDRD9EQV9JK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87PMD3VK9F7UV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87Q3U98JZ2CBD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87RA778VRQ74P', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87SAHAKEF3ZBH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87SBWBMTUXZLY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87SQXRRY9R2ME', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87SX9TVTRM8EU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87T5V3VBAWVZK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87T838A7HC76L', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87UYQRAAPPZST', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86H9EAR227GLQ', 24, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86W7PS9T2FFYR', 26, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87V4QF7WCZX76', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87VA7EP9MVMZC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87VREMCT8FWK7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87W69S7YMVSBT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87XAEL64S8BA6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87XHQFY3UTZEF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87XSBYWKYH3S4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87Y6RJPYCD38D', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87ZBVPFEW95WU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87ZYSRRJQ72AA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8823BVCAE2QBB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF882PLSQRTXYUG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF883FTM82QT2KJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF883H7Z269BKGT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF884MJA7L9BBP2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8864TKCXYXEAS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF887L7C4R6PJ55', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8888WJM5LC35Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF888SFED9BYRQV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF888WRA8Y9C6TJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF889EQ4KG3HX3U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF889UFJUMZDHMR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88A8F8YX7CW29', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88AQ8WHWAPQM2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88AT57E62FB34', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88C4ATXM479S4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88CMZWHZ5MH5T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88CSF3B3Y43ZH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88CZ6BV92SBSH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88E6FU36JA3CF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88E6PSV9SS4GV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88FMSY22TK424', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88GTTC2HMCYV8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88H5ULYDE8HAP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88JFQYRDBQ55R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88K3FUF4T87DV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88K7ETC9GHSZE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88K7PCGQZMXGZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88KW9FAFRDUZ4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88LWC7T7CWW4W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88LXLWLMWPKCZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88LYQM3AYHLDQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88M3VGJUKCUAH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88M4D7MXAHHGD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88P6V3TATL6HP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88QDTQ7AMB66U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88QHQUKUVK28F', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88R4ESUTHS29B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88RAWXRF97AFR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88RHPKV9HTBFW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88RPGW3XDEMXH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88SFMPEGYK4KX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88T3EFH263KCL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88TKPFWLKG8ME', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88U4R4Q9PUWFS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88UCV7ZKQMDTB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88UF97ALMCEEV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88UXK7LCU9YAS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88UZQ4FSLTYHF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88UZY9P44MTLA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88V73CMRJR98Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88VMJK75RGPVZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88VZCX7C28CLZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88WDHBCAWUH9X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88WTFCBJUPPW7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88WYT9VHULVFK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88X6GPLLLWBAT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88XHKD25MZ2LS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88YXS2XQG4HMP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88ZS9X3A4RYAH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88ZWMZGEL32YG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF892DDDZ7BLPQW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF892M7ZVY2ESM8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF892MJ8XYSWBYJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF892TBH5ZQSQ46', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF893QKHKBM7C2A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8944YADGAM4KM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF894FE68WMZL7R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF894PV4V3VDXMY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF895VHYRY4BG6E', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF896PDPB3JPQBD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89739TMRWXM5X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF897CFP65YT4KQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF897MV7P8U2T47', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89899WGLYC4M7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF898HDB3SPZWMD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF898HZXSCTJAYC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF898QD7G3LHLZG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8995UWXTYYB7T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89ABFEGSELDEB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89AYRXU8ST87Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89B8A82VG8GYM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89BDK9LJT7JPG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89BJ4T4FBQB5D', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89C64C76UWMT9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89CS7M7XDVSSZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89CX8PEW7EEPX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89D34GKT5D27X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89E8UXK59WMVU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89EBE39K5ZW3Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89FKB3LRDVXK4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89FSLEEQXL74T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89FTM65BMWS2U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89GTKT58FWFFZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89J4F8LZ5KDYW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89J4XS2S3RXJD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89K8M96YR5JM7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89KF8UF3MEH58', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89KJ9BLS7F4HD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89MC7MVCPLZC5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89MC86HD48BK6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89PG6BCDE4M72', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89QJ3XW5XELL6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89QY4ZAQQ86DX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89SBRJYC6WL4S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89SW8LVBHW4WX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89T3YF6EATCAF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89TMLRB9SY2R5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89UUX4R2MFV8U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89V9VMCBV7MQ3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89VVF5M752CC8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89VYDS3RKPUU8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89XHFZUM8CSDZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89Z3FDC2TGCBS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89ZCJC2TYL2HK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89ZLE2FMFGD8Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A37KDVXBU8B3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A37XVMRV84U8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A3BTUXZDFCX3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A3RWH3HLBSGE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A3Y7LUSVTJSB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A4296CZQSCFB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A5Z9CDHSCZCV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A6WUGUT3S7GM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A84E3L6MT9RW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A8XMR9UL6HLX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A9KACJF4JH2U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A9LYTD4CPSKX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AA8DTVM4C7WR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AAZXAXXS95YK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ACTRF9QFRK7B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AERU7SCKLTR5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AEXDXPFQ48KP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AFVG6BRKPSPF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AGUTWSBD4FD2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AH3YSY36F4M5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AH4JMZDX7QDH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AJ3CDVCFJ96T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AJBF2KAVKSP3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AJPUXD7BXS4T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AKEUT2JTW3ZP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AL2BZSU26FLU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8A9WCVL6UETY9', 33, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF88VDAG86A74CZ', 40, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AL852G955SXT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AL9QL9TDFSGU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ALJRHGXKDLCF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ALY2XPB2UA2H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ALYYP9JSHVK4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AP3A8FYMF9FQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AQLA6QSYHSAP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AR5CPT23MATP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AR9X5ZKRK863', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ARFZ46FWUCRW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ARUE4RKF7LBQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AS3A25MAT2JW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AS6K8MEAGHXX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ATH7382R7Z3M', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ATL7JHGTHTZZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ATPXWR6DT9HV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AU4EMY52YW26', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AUM2PC7TL6UQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AUTJ5B2YYZ9D', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AWLWMM7DF2LB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AX86VLRVMR7Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AXBGMCXSK7UF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AXYMPE5WWET4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AYEMYS6S34FG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AYFPJ49G96GR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AYH6XUYWJ8TV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AYJFLRKG2A5S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AYLGD66D9AXT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AYPYRMLXAS8T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AYUJCBVH97SC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AZ46JPBBB4KF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AZV7HGG3HB44', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AZVZ5QPGYW77', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B24FSXZ353WS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B26ZAFXHAQYU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B283EGJ8RVE7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B2WBZEVV9GRB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B2WVZR8X4SH6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B36PJSC9PMHJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B3FMF4QKHBRK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B3GJAU7MYDAU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B4LEVFDAUPHX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B5EE3CAKW3AT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B5JUXVBT77C6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B5UGYV7W23M7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B764P4WR8FWR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B76VF32PF3SU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B8J96YYXJTFA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B8PQPFL5DZJ4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B94U6RB7XAJQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8B96SFPHRE5CM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BAK2JWXUV2HF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BAX9ZPG6PJTJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BBW72TF6C22S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BD9VPLW58YFV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BDVV5LV4ZBEH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BEJXMW4FQ6LG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BEUBFP8KYRHC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BF8ARAEDXKXS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BFMBTVFAQTTP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BGE4C8CBXR6Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BGJB98WKAX3T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BJ4877DHKYDX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BJ5TZX6465XA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BJFZH3FBL8B5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BKEC86GERHLQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BKL3QS7PEK93', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BLLYJUUVW3PT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BLVFCYB97JB9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BMWLF538EVQL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BPG8ZJ43JJ6H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BPWGWR3R8KS6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BPX36XYUALX6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BRFJMH3HKLWH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BS4GAZAFYYXX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BSBLBKVMTBJ5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BSMYALWP7VFY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BSP7GHFUVZ5B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BSP7HKWVQXQ8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BT2WCYV8VZRW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BU84SKKAP3BC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BV2WJMMM3YEP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BW9976A6RQ28', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BW9LHFS4JDRF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BX6HA394FLT6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BXXAQEDEUZHG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BYBKXB2F46HY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BYHXDX2TL8V3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C27C9EJGAE4Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C2PLW7RLB2LF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C4E3CBVCR4XH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C4MA6BEJ48EV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C4TTRWHBXRBS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C5MYX8VX7EPD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C7LGSHPCYRUE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C84QSL9237DT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C8FMDXUYTRGV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C8GXLY4E36JV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CADQSDEFTJ3B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CAWHTHFGCLUK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CBX2A924XWBG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CCQTUEUBZR3T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CD3JLUGL92GX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CECWY7VG7896', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CEW3ZWW6EF29', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CEXXSA4L7BT5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CFHAUM45LWAU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CGAR5RT4L9WJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CH575ZPJCT29', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CHW4LWBJ6CMT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CJJSD62AYJYW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CJRE67UR6Z76', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CJS3JPCLUTQA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CJX3KKK32BP9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CKBK6CLJ4TPE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CL6PBV4MFW72', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CMCPX8F64ZSJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CMFAVKZT66E9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CMQ9EYQV8FD8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CP25ZMAKJ3X7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CP7FVKPHEABU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CP9HLZ9ME782', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CRH9Y5QMC66A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CSFA6BVZLTVY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CSUUCB9MJEFH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CTZDQJWMM8C2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CUCZ6BYSQD2L', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CVMW75H5VU7X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CW4LWCQXECTV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CWPJHHVL5CHU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CXMXVH9ZQQ7V', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CXYBHTJB3ZYH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CYYBJ9DTVHY5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CZTTYVZHBPXD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D2CYVGEBMUR7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D2VZV3XSAE99', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D3AGBWLSC7R9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D3BXYS3Z7B68', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D3H2YED7373Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D43D7J2EQJMS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D4F7XYD3RQ2U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D4J26LL3YYC6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D4MKHR8HTSFV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D4P9ZTTU9UUA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D4X9AVCPC3ZE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D69EBMUKDY5J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D7KHKFMZ24R3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D7WV8CGKUV9Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D7XMDZCVH6B2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BYXPXDRFZ5L7', 30, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C7ECUX3E7DJ9', 34, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8CFMAZR424ECG', 46, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8AW5TBEPTH45M', 51, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C9RQUC4LSAA8', 52, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D7YJHY9E5PX8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D9AK7WJZJ9YD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D9FDGWQKMXGR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D9MZMRT24L95', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DA9Q89MJG24W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DAAV4VBPBKKT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DAFM9G4UY35W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DAKQ9MWJ7KFK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DAQS2BZMJJRG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DAXZZ9KVYUDZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DBVLSFZ4KMV5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DCHHTQEM5VSB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DCHURK2MLVHR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DCQSJX77EZ6Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DCZQ5939XVDU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DEH6WE2G7YX4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DEHYXSAEPD2R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DFYQSE7Y7595', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DGQ449VBQ3X5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DJ534D5X47TT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DLGHFKBHD3KL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DM3MQ8CQ3B8A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DPW7FAHYPTVZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DQ5YTQY56PZY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DQC274Y3XC3W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DQX6YWK7EC93', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DR99XJB3KPAD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DRGPUESPYVWU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DRSFRSAW8AZD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DS8AHDKDHKBQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DU26Q7VLQGWF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DU2XYUAASATT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DUJGYUC3DCZ9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DW3RVLSAJBCP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DW5PU32X45QK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DWHQ5Q7EDMX6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DXKJMC245KLB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DXT8QR3X2JCQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DYZHUQY2U4A7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E2HDHRBG4G3A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E3XRJ9QA7V2U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E45259QBVSQB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E4ERZTZU3MC6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E4S8SUTTES37', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E4WJMV63AAT2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E4XUSVUXTZXE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E6EUFBFQJXA6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E7QR3KE73BYY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E8SRKSFSTTBS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E8SVGUF2P9ZM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E8W6W9F97L7Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8E9FWPALT6Y46', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EBKXFQ72HV4W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EBTS9PRV5EJX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EBW654RF3HMW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ED3CBUHGH3EF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EEMR8QZ2VELY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EF6SQAGZULF8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EFK98RFB3DRD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EGHDBXG4BKV2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EGHJL4TPFSL4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EGL2GRWJZ9X7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EH5MCTYU74TC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EHP5V7LHT5FD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EJ3Z9SHZP2GP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EJ4FJ5VJZ6DJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EJVPEMLRVGUE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EKHZJA93JS6J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EKTD9XKV74M9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EKVJ2UC2AY38', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ELURJM88C4EJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ELWR9UD5LEYF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EM8AVPKTYESR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EMB6BFMZ8Q83', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EMF46VYJH6DM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EPQYS4Z3BKHV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EPSPSJRWMS4U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EQ4MPHEJWJGE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EQB7JJUUC69Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EQSW84HGDCRQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ERAHQKZC4BYQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ERSRJFH4ETBX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ES2KJJB6SUTH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ET43DWMX2A3C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EUAY82UH3LMQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EUEPRD29ELX4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EUM2L92HF6MG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EVJTT8EQCZR2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EW7EG3WG3KCR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EXF9SXF2DZJJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EXM74VGAPSDK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EZAY82QMJXYW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F2EY4EKMMKEP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F2FU7HRK6MR8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F3WT9C63E7LP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F55KP8LYJPB2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F5K3W5K9SARW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F5W9DWBDSQWS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F63BCUCJ72T9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F68LDDT8PQ2A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F6HU3B7TZLUQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F74558XQQR4U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F7Q6JHS4BVHZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FA7Q8AFPGWY3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FAVUAEE7SVC2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FBAMS8LK5QYJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FBGJ57P4JSUL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FBT4DB5AUW5W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FC79GKAAYK9B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FCYRG333QS5R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FDBB3GM4RASX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FDEEKQLS6FVA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FDSHL987E4AK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FE8Q4GR83Z8B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FFPLMHW7FLF3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FFUZ3SRM66A7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FG2G3DYJ738X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FJA8S2XTX2MY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FJT9MFVAW995', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FKBMW6JMCRPS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FLQ2DBLQHJDQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FM6SPCYZ5JZX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FP8PFMG8SZ5A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FPLK6YDTDJUK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FPWSJMWZ8X28', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FPXL9LYTBC82', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FQUA8FYMFJ47', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FQVMDM78RRRB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FQWU33WPJPRZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FRVFZGYBHU49', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FSPHUTY9DR8D', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FT5K2DKHJ25P', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FTEKYGBBTRDZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FTUYVH5QGEMA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FUA73VTY2UD7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FV2SK6A2MBQ7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FVFDWB6LZXLQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FWBH2W5XDKSV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FXDJZ7DEAKGV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FY47C7DA8C66', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FYSS6EFP8GVP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FZ5TEK4JQM76', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FZJ3C87SQL2L', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FZJWJMULJH39', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8FZT222VGJXQE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G377WC2UZ3K9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G469ZA3QE64Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G4K37Z8YB5JZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G4V7G6JMGL9M', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G56JM63TBXJU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G5G2MDSW7CAX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G5ZLUV76E8UR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G72LWUEBMFC7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G7XKQJA6F4FP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8EJE75ASPRDBU', 48, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G88HWXKQGKH2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G8MVJFMB7DZJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G99SCKGHDTGX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G9A6WL9XCEZ4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8G9FJ9EEYQL2B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GAB8ZPAGHKKU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GAZA9JKR9JGW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GBP7AVUMF2AJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GC6B7RVA8SKA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GC99HVF4BHYR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GD9ABGQXM4PW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GDLAC8RB7DTW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GDWB8JE425BR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GE6AFG2KWCAB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GFBVSMKEJR6U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GH2H3T54M9R4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GHQ2J8HGL7LZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GJ2RPVJSU6E4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GJTDERFAMDRV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GKR39JMKDDWJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GL4MY796AT2V', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GLMREB42RMPE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GM268DFJHC4W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GM5BV734JMS5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GMCPC8W4PEB2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GP488JC246YH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GP67EK9LLW3U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GP9EP7EGQHF5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GPLWW6JBP97D', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GPZGHYDRQWJH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GQ3K8WKZPYCH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GQZKPE43P9Q8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GRSJ2C9XS9BE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GS5YEXLKE7KG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GSK9A9RLXGEJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GSKML95LHQCX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GSYTT5CER49G', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GTP9T5K6XPEX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GTULYBS275KT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GUYE7TEH3BHR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GV5PPMP98ZTW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GWRDVA6DZE5J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GWRU2H9T9B86', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GXEK973ECLBV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GY7D5DPSXFD8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GY9EPX556UMF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GYDAVU26X6F8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GYJJWQSFADU8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GYT8GH66JSTG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H27MRCPGRYX8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H2J59UU6CSBY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H2RCQTRXZTFL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H56HL5FW33K7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H5CEWYYUH348', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H6A7QTLMDJSQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H6DL86YFSGDU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H6PZFUR2Q6D3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H6XDKC5QK59J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H7GXR7URK2Y5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H7S29LTWVTLR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8H7SMW6ZFAT5V', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HAUEJRYZU5AF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HB8FXE726UP5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HC3YVLYMB8CE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HC6WTD697JTQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HCR329DB2AAC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HDHK7SWKPV4E', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HETEQFY27EHA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HGKF4Z7K22EU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HGSGQM6KESL6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HGX6TEK96MQX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HH8E9URL6PC4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HHJTCK6T26B4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HK6L354LJU4Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HKCWCTAMDR44', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HKR9FFY57Y9T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HKURR3BRG3R9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HLW4BMEM56D8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HMDH7J3Z2XJM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HMJYQVV7Q7GE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HPAJEMB69DQ8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HPBC6KFAF4H5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HQ2P76K6PVTX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HQAHURASS7CJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HQCPE35BGEFF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HRTF9RJRAEW5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HS2YR5Y48JYV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HS7TYUU34539', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HS86SZRXVBA2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HS8Q6XJTZ6BK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HSC8APZBTU4H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HT7G5SKBGU6Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HTR84R6P4ZZ6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HU59F5RKSXU4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HU65D2PGPMYL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HUE8F4ZL5SHQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HUTQG84JQTKE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HUXDABSJA2RJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HV7ZM6JXEJ3L', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HV9AWS6AMU2R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HVAASGVKSSB8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HVVDEW9DCKQF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HWDZ3XKQUQTC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HWHQKSVA7FLB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HYX72V9WFD5Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HYY7B3KZ2KXS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HZ45GHF9EPJ8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HZ5WLD4TZEQK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HZ6XCTX26ZXK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8HZAVGFLGWP67', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J2LHU8CVU74G', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J4VLEJVYYRR5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J4WASLBM6KM4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J5CHQQSDQD4T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J5JZPJC9439J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J5SEE9VR39DU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J6343ZRB8AU7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J6MPPDLVJJBY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J8UH4V9PVERT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J9764698L95M', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J97V8JBCTFTP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J98C3CEYRBL8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J9DK7649TLBZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8J9QAXKD25W4C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JACU6KUHXKXQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JADGH7CX52PY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JAG5HPSRQPRG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JB7LJ42JY2KL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JBF9KXRL6BHV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JBHHWDMR8JBW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JBXCRL7JCAGY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JC4J9GX6H23Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JCKSJ97TSCWD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JD3UMXMW8SLQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JET58L8HF3TR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JF98CA8BCHVC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JFGLFZ7Y6F95', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JFLAE85FTDM3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JHFVXGP4CS3Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JJ35B8ELG8CX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JJ36SL2X9S3A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JJDUF9C6B3RQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JKTMMGXCFB65', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JKW5M5F87SD3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JMPB84QJPVF3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JP8S5VAX5XYC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JPH9T4QMHW57', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JPRZUCTFRFMH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JPU4PD8A5Y8F', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JPVRQ67669E7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JPWPK3DBR9GZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JQYV3UK52Z34', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JR9CE9JEBPAZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JRAXERJ58FW4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GATTYPHU3ETV', 53, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JRZM98RR8ZPD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JSR2UT5TCQVP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JSY78CGTJWZS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JT2EYDGQWBME', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JU39W9YXQYT6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JUDC5QWV9BHX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JWB2JGFPF7BZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JWD83HBVMQFT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JWZMUWW73UF3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JWZU6FQQJARF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JX2QP32UGZFZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JXR9HH89MSQK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JXY9B8YU868B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JYRVB7DXKSM6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JYWWT2RYTVJ2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JZ48LDYBWPL7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JZK2QGFKZEPG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JZK88U3SF455', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K23WDCT7S5VP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K26C3ZRP6ZZG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K46VAHE3HZRT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K4C4C4PVE27W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K4URTWX5E2WX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K4V4ETF6BALA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K533BDVS26BH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K6T95K6Y8GMF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K6U47LR6PGFA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K74SFXLDUSD2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K76UMB3R3KVX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K78MQEWLBQD2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K7KU5PK4CPSR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K7P3CX3FB26K', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K877WD4GFBTC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K8RKU9S6658B', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8K98FSVDE394A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KC8KGQZ2AAZP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KD39DJHL5XQZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KDBQAAVPBV7F', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KDF4ED3WB7YV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KE2BE5XQGZCD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KE3Q5TKFCJZU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KE7RQEJ3Q32V', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KEB6VVMXFXGZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KEE445GR3MET', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KEFFU5TKEGRK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KESUBTKJ8FXG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KFAMVFXW2ATR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KFY56JBYG8VQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KHZM3UGZPY52', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KK923MTP9VST', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KK92F22Z8983', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KL7V795C4LYL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KMCSJSDYBTDH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KMVJRZQ5ZLDH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KMYMDH7XXB3W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KPKMZU6A7HFQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KQ8HB3M92KEF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KQLHFEBXXKM4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KR7XABTEU7BZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KR9T9LWQDQ5M', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KS6V5VBRCH32', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KSKH6KEF8886', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KSZ4B3XQZQLV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KTYUH8LXP6FV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KUBC2DJTS3JM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KUBXYCUYV6BX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KUP5BQ6RE26Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KUTRVT86D6S7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KUXH8ZLFXB4G', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KVYSJSE4XHVF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KX5VQKGSWBZD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KXHY6YLBW9ES', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KXJLVADQ6MS7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KXM4U88C7SYZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KXMHKKZ24RBT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KXZ8GDMYPA9S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KYG7RPC5UU49', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KYVF5YKS7M9C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8KZV4JX6SMP2K', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L2ARJ6DM26GJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L2T92W75MSXS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L3X8JYKG4V34', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L4PKHPQ6P5HV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L4V57LF95893', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L4V9LWJDVFAV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L4YYQ4TTD28K', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L598ZLLVFYCH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L5D5FQF5F4U7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L5FWLP79VRDG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L5MH4BGHKLPB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L5XA652DK382', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L7FSC4LFXGCR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L7R8XAV45AJE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L7W225PD7LV9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L85X3ZLSRF9L', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L93EMUAR3UKV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8L9DQMFEHLSMV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LA3EEMCTAAT7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LAFL53JSG8GP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LAR7ZRK9XM6K', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LBFZEGKJFE8Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LCCPMSL9XQDR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LD5G8UWPCP4R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LDQLD7SVCC8L', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LDRUXGXRWSJX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LE56E4M8LXSY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LEMLHAGFXDKH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LETMDHLDEJHF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LGJMGABZPEUX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LH639UQWVZ26', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LHE9B6U9MPUP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LHQSRY3BA96C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LJ53LH2SCJ3T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LJAAMMFPVMDX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LJD9QGDWLKQG', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LKJJMRRAJP64', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LL8WEUWXCD2P', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LLSTT2GZS33T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LM5TLZHLAKC5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LMM8PYRV7UD5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LMMV75MA67T8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LPSBGEK6LHVR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LQB8R3U858ZS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LRTDJ43RXF3H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LS2GQWC93L7C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LSGMXY97QYT5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LSMY7RAARV7T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LT7B2AZE7UBV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LTHE4E8QG44C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LU75A4QTKP25', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LVCQKBMATQ69', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LVLHGLUJWKCW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LW46QECQAKPJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LWCU779J744J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LWD5V3A9C7SY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LXW5KXSQTPYC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LXYR7LXEYXSZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LYAB2BY5QUVL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LYVLXXELP724', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LZ8KDA5T8ST3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8LZU9TUWMM37Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M27LLTKVUREV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M2FJFX3Q64K7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M4QHK49TXACX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M4UTKHLXEZ6R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M5SAJV4TS4K3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M6S3GPQPVDRL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M6UR4DKVDGEK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M6WBWR7RXBC3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M7QDRGUKKT8R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M84QBBKCH7FM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M85GZW8K8BG7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M8ARRHDGDKDY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M8BEACZRDBR9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8M9TFMER8ZRRS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MA8SR3Y2PVWK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MAHQLSF7DV97', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MAUSW5V7RRBQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MAZGSGWKYC33', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MB8G2AJJPHBT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MBAL7GMYZR8Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MBXWJHFCK8GW', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MCET4UP6UARR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MDKVM7CW3FR2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MEBMS6V23BRS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MF6JRQ79E899', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MG6SXQQDSBYP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MGXCJ6WK42L6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MH8FSFTWGLTA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MH8TQA86CQZJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MJCJ8R2VY43L', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MK9KH79J2L5Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MKCMJA4ZWHP3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MKGKVF2X7GC2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MKHKQ9YGTZ4Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8ML93VDUP6W2Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MLF6HVARW67U', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MLUMSLSL3BPM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MLUZ75CT7V3R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MLZF9CVLQV48', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MM7BMRFBY6ZB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MMR6EGCY55TV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MMUUBHZBAV2M', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MMZ8Q66D37LK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MPCL8FLPC7GQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MPUHSJVF7BSH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MQFGTBPQVCSV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MQZLYWZ8VS62', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MRGYQZ8LCET8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MRY6GJXU32FX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MSC7VGUPKWSA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MSGLPMTG6GF4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MSJUPQ68BLRV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MTDPP2B9KPR7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MUPUYEVTBF4A', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MVBF7R3K58GE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MVHCAA6LQPBM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MVMQB2UQAKBS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MW7KWDKDUX2S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MWF8CHFRGA2M', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MYV4MHCWH3TL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MZBPXEADVGW5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MZTRH39PR4EQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MZYGBWS7EPUQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P22J5AHA4FGE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P2VYU7A7PZ9Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P2W8UGVCCZDK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P2XF2UGW4KKL', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P4A6EBSJ2553', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P4DC2FRDGQT5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P4X84DXQ2CK2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P5A9TVL9FP64', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P5SAYYTC4YX8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P5XDS8QF94DZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P68UFUVU3DD7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P6HEXKMDES9R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P7PWHQB4GK6X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P85HD9QB2259', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P8K7RB67CMRP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P8LHLCWA9RQR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P9SBRYLCMKT5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8P9XBDSEVDYV7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PBELW2JF2XVR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PC8BU9TXHADB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PCUSD3PEU74C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PCXABHWRM5RD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PCZRWT4TEGYX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PDPQ5AUY4RTP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PDPRX63JG8B2', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PDYB2FKHDQGA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PGG7EF3BYU5C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PGPSAAYAZ9QU', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PGQ7T8B5ZKCY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PH78R33Y4YK3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PHEALAHXX5RT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PJ5RYDGHL9MS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PJZWLSWD2HFH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PKDATB36GEG9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PKJSESBY4K2Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PKUW8LMK2WAV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PKW9FQA5D67Q', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PL7HQYBWKWQC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PLBB93736R7E', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PLCALFGLMPDM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PLL4GMC7XCBB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PLMQF8MYFRPA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PLZHH6F2TSEF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PPLCGBFGVS87', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PPRX4FX2H3Q5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PQ5UXWX6S3E9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PR35ZV37PE2S', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PR4GBXFZLKFF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PRR4YBXHJFAA', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PRRKRZ8MA7ZF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PRS5EBA92P5T', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PS2TPUGH6VQ5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PS3YZ3WYDEYM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PSDZQSTW4MEZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PT3B4RLCSCX3', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PTAQA2PG5YRS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PTCZQ2SUUCWV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PTDTHSJPUFCZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PTPJARQ5L6XM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PTUGVL8PT26C', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PUD44Y85LKTK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PUEWU76V2UCK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PURSRGXLXRKX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PV2SVQZP8H2W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PV3T3M4FXGYH', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PVG52MXDLU4W', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PW9JT3CQW7HZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PWFG25CSY2PC', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PXFSGW6LV3KS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PXW97E4QWWQT', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PYF8CH76437Z', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PZAXTBWVY92J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PZCPPTVX5HHD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PZG93DF4T6K5', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8PZQ4MVCVJ6QK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q2ETEYTV4289', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q2YJSML9TDZZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q3QRRBK9XYXZ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q49LW67L4HQ8', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q4B4PWTCVFAD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q4C3GMVXPTUB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q4H3KZLGCHE7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q5HM3R9466Y9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q5HSMGRED353', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q88XCVE9S8Y4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q8G3R7A6WATR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q8VULP76XGJE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q93YWK53WCDR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q94A6FVDWZTS', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q9GSBTMT229Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q9JTT2BVCPJR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8Q9UF5SA4FTRY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QAG5D3UC5BCQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QAZ5AXU5BGZ9', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QB8DMCC66UEE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QBLYR7DAAQEK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QBWKQLFJU2DE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QCGCWFGMTD3G', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QD2CT352R2TX', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QD9VKP5XWAS7', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QDR94JBRTFPV', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QDXQMAQL8Z6X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QEHKXXAMP7WD', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QFG6PMTJM5YY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QFHQBJX8ZD7R', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QFUUSDMT6FKY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QGCC7TXKMXJR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QGEG5W3T263J', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QH394U9M7PUF', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QH7M6RBLDGEQ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QHRVHM9Y2PG6', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QHYWMH96PY8Y', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QJYQRJRD6QLK', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QLDT4ZM2WQZ4', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QLHQQUVBPJJJ', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QM67J8QUUQXB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QPVEGLZ5L529', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QQ3XC9XLCU5H', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QQ96PJEL9JPY', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QQE2XGGYUUJB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QQF5SH9JBASP', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QQLTJX3MCPGE', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QSQ9Q7DDULPM', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QT87525MQVCR', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QVRPRFJY6XGB', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QX4R4UR55W8X', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QYUM4VZ82J68', NULL, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8R3SQLWR7LJ9H', 1, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8R3BBLWUD4JX5', 2, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8R2VTUHY4DTM9', 3, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84P4B479YQD22', 4, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8R2KMV7YELRCF', 5, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QZZ5GAK5J7ZG', 6, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QZUPM2BBYKCB', 7, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8R26MZJXULHJP', 8, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QXR9X8FD4GK2', 9, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QXC3Q6LDCEQF', 10, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QWUR29KYUKFQ', 11, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QX8QUVRQ2SF2', 12, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QVF9THDK5Y28', 13, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8R23M4EG29FKZ', 14, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8F2KQFXY278ML', 15, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QV2SG3PXW8UW', 16, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QV5945CLGWXR', 17, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QUZVT4QVEE8W', 18, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF86ET6XHYY3YTU', 19, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF84HLVA4P32PWD', 20, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89YXCM2FHK89Q', 21, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87D8MMQ73TLUY', 22, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GYR9CERA948Y', 23, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF87X5KPXX9BLF5', 25, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8JZU227UCVJE7', 27, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8BB5HZRTDAGEZ', 28, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8C3AGQS55KF3W', 29, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QU6X8BVHWS58', 31, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8DEFJTDBFMMVQ', 32, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QM6GMCZPZ45F', 36, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QPPY87QVFBBG', 37, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QP5LRTAEB3FY', 38, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8D59EZJS63ZMZ', 39, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QRUFW28J8PZG', 41, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8MXL8RVCMEJUY', 42, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QML42Q2KV655', 43, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QLB7WX93EMY9', 44, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QTJ3JH9F27R7', 45, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8GJH6L85LPBLL', 47, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF8QJ2CPT2XYG3F', 50, 1);
INSERT INTO "public"."unlockables" ("code", "player_id", "promo_type") VALUES ('VCF89455ZYR3WZAT', 54, 1);


--
-- TOC entry 3146 (class 0 OID 0)
-- Dependencies: 184
-- Name: instances_id_seq; Type: SEQUENCE SET; Schema: public; Owner: m2tm
--

SELECT pg_catalog.setval('"public"."instances_id_seq"', 1, false);


--
-- TOC entry 3147 (class 0 OID 0)
-- Dependencies: 182
-- Name: players_id_seq; Type: SEQUENCE SET; Schema: public; Owner: m2tm
--

SELECT pg_catalog.setval('"public"."players_id_seq"', 54, true);


--
-- TOC entry 3011 (class 2606 OID 16564)
-- Name: instances instances_pkey; Type: CONSTRAINT; Schema: public; Owner: m2tm
--

ALTER TABLE ONLY "public"."instances"
    ADD CONSTRAINT "instances_pkey" PRIMARY KEY ("id");


--
-- TOC entry 3005 (class 2606 OID 16545)
-- Name: players players_email_key; Type: CONSTRAINT; Schema: public; Owner: m2tm
--

ALTER TABLE ONLY "public"."players"
    ADD CONSTRAINT "players_email_key" UNIQUE ("email");


--
-- TOC entry 3007 (class 2606 OID 16547)
-- Name: players players_handle_key; Type: CONSTRAINT; Schema: public; Owner: m2tm
--

ALTER TABLE ONLY "public"."players"
    ADD CONSTRAINT "players_handle_key" UNIQUE ("handle");


--
-- TOC entry 3009 (class 2606 OID 16578)
-- Name: players players_pkey; Type: CONSTRAINT; Schema: public; Owner: m2tm
--

ALTER TABLE ONLY "public"."players"
    ADD CONSTRAINT "players_pkey" PRIMARY KEY ("id");


--
-- TOC entry 3013 (class 2606 OID 16596)
-- Name: unlockables unlockables_pkey; Type: CONSTRAINT; Schema: public; Owner: m2tm
--

ALTER TABLE ONLY "public"."unlockables"
    ADD CONSTRAINT "unlockables_pkey" PRIMARY KEY ("code");


--
-- TOC entry 3014 (class 2606 OID 16597)
-- Name: unlockables unlockables_player_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: m2tm
--

ALTER TABLE ONLY "public"."unlockables"
    ADD CONSTRAINT "unlockables_player_id_fkey" FOREIGN KEY ("player_id") REFERENCES "public"."players"("id") ON DELETE SET NULL;


--
-- TOC entry 3141 (class 0 OID 0)
-- Dependencies: 8
-- Name: SCHEMA "public"; Type: ACL; Schema: -; Owner: m2tm
--

REVOKE ALL ON SCHEMA "public" FROM PUBLIC;
REVOKE ALL ON SCHEMA "public" FROM "m2tm";
GRANT ALL ON SCHEMA "public" TO "m2tm";
GRANT ALL ON SCHEMA "public" TO PUBLIC;


-- Completed on 2020-02-27 20:36:37

--
-- PostgreSQL database dump complete
--

