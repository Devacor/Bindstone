-- Compatibility at language boundaries only; game data uses ordinary Lua tables.
local array_mt = {}
NULL = setmetatable({}, {__tostring=function() return "null" end})
function _j_array(t) return setmetatable(t, array_mt) end
function _j_truth(v)
	if v == NULL or v == nil or v == false then return false end
	if type(v) == "number" then return v ~= 0 end
	if type(v) == "string" then return #v ~= 0 end
	return true
end
function _j_str(v)
	if v == NULL or v == nil then return "null" end
	if type(v) == "table" then return "[array]" end
	if type(v) == "number" and math.type(v) == "float" then return string.format("%.6f",v) end
	return tostring(v)
end
function _j_add(a,b)
	if type(a) == "string" or type(b) == "string" then return _j_str(a).._j_str(b) end
	return a+b
end
function _j_at(t,k)
	if type(t)=="string" then
		local result=string.byte(t,k+1);assert(result,"string index out of range");return result
	end
	local key=getmetatable(t)==array_mt and k+1 or k
	local result=t[key];assert(result~=nil,"missing element "..tostring(k));return result
end
function _j_set(t,k,v)
	t[getmetatable(t)==array_mt and k+1 or k]=v
end
function _j_idiv(a,b)
	assert(b~=0,"division by zero")
	local q=a//b
	if ((a<0)~=(b<0)) and a%b~=0 then q=q+1 end
	return q
end
function _j_div(a,b)
	if math.type(a)=="integer" and math.type(b)=="integer" then return _j_idiv(a,b) end
	return a/b
end
function _j_sar(a,b)
	b=b&63
	if a>=0 then return a>>b end
	return ~((~a)>>b)
end
function _j_substr(s,start,count)
	assert(start>=0 and start<=#s,"substring position out of range")
	if count==nil then return string.sub(s,start+1) end
	return string.sub(s,start+1,start+count)
end
function _j_starts_with(s,p) return string.sub(s,1,#p)==p end
function _j_ends_with(s,p) return p=="" or string.sub(s,-#p)==p end
function _j_find(s,p,start)
	local i=string.find(s,p,(start or 0)+1,true)
	return i and i-1 or -1
end
function _j_rfind(s,p)
	for i=#s-#p+1,1,-1 do if string.sub(s,i,i+#p-1)==p then return i-1 end end
	return -1
end
function _j_find_last_of(s,chars)
	for i=#s,1,-1 do if string.find(chars,string.sub(s,i,i),1,true) then return i-1 end end
	return -1
end
function _j_to_int(s,default)
	local v=tonumber(s)
	return v and _j_int(v) or (default or 0)
end
function _j_get(t,key,default)
	local v=t[key]
	if v==nil then return default or NULL end
	return v
end
function format_value(v,spec)
	local digits=spec:match("^%.(%d+)$")
	if digits then return string.format("%."..digits.."f",v) end
	return _j_str(v)
end
function _j_coroutine(fn)
	local thread=coroutine.create(fn)
	return {
		resume=function(self)
			local ok,v=coroutine.resume(thread)
			if not ok then error(v,0) end
			self.last=v==nil and NULL or v
			return self.last
		end,
		done=function() return coroutine.status(thread)=="dead" end,
		get=function(self) return self.last or NULL end
	}
end
function parallel_transform(a,fn)
	local result=_j_array({})
	for i=1,#a do result[i]=fn(a[i]) end
	return result
end
-- The benchmark has SND_ON/MUS_ON false, just as in the original.
function play_wav() error("audio is not part of the headless comparison") end
function play_midi() error("audio is not part of the headless comparison") end
function stop_midi() end
function midi_status() return "stopped" end

local utf8_char=utf8.char
function from_json(s)
	local at=1
	local parse
	local function space()
		local _,last=s:find("^[ \t\r\n]*",at);at=(last or at-1)+1
	end
	local function str()
		assert(s:sub(at,at)=='"');at=at+1
		local parts={}
		while at<=#s do
			local c=s:sub(at,at);at=at+1
			if c=='"' then return table.concat(parts) end
			if c=='\\' then
				c=s:sub(at,at);at=at+1
				local esc={['"']='"',['\\']='\\',['/']='/',b='\b',f='\f',n='\n',r='\r',t='\t'}
				if c=='u' then
					local cp=assert(tonumber(s:sub(at,at+3),16));at=at+4
					if cp>=0xD800 and cp<=0xDBFF then
						assert(s:sub(at,at+1)=="\\u");at=at+2
						local low=assert(tonumber(s:sub(at,at+3),16));at=at+4
						cp=0x10000+(cp-0xD800)*1024+(low-0xDC00)
					end
					parts[#parts+1]=utf8_char(cp)
				else parts[#parts+1]=assert(esc[c],"invalid JSON escape") end
			else parts[#parts+1]=c end
		end
		error("unterminated JSON string")
	end
	parse=function()
		space();local c=s:sub(at,at)
		if c=='"' then return str() end
		if c=='[' then
			at=at+1;space();local a=_j_array({})
			if s:sub(at,at)==']' then at=at+1;return a end
			while true do
				a[#a+1]=parse();space();c=s:sub(at,at);at=at+1
				if c==']' then return a end
				assert(c==',',"JSON array separator")
			end
		end
		if c=='{' then
			at=at+1;space();local a={}
			if s:sub(at,at)=='}' then at=at+1;return a end
			while true do
				space();local k=str();space();assert(s:sub(at,at)==':');at=at+1
				a[k]=parse();space();c=s:sub(at,at);at=at+1
				if c=='}' then return a end
				assert(c==',',"JSON object separator")
			end
		end
		for word,v in pairs({["true"]=true,["false"]=false,["null"]=NULL}) do
			if s:sub(at,at+#word-1)==word then at=at+#word;return v end
		end
		local token=s:match("^-?%d+%.?%d*[eE]?[+-]?%d*",at)
		assert(token,"invalid JSON at "..at);at=at+#token;return assert(tonumber(token))
	end
	local result=parse();space();assert(at>#s,"trailing JSON");return result
end
function to_json(v)
	if v==NULL then return "null" end
	if type(v)=="string" then
		local result=v:gsub('[%z\1-\31\\"]',function(c)
			if c=='"' then return '\\"' end
			if c=='\\' then return '\\\\' end
			return string.format("\\u%04x",string.byte(c))
		end)
		return '"'..result..'"'
	end
	if type(v)=="boolean" or type(v)=="number" then return tostring(v) end
	local parts={}
	if getmetatable(v)==array_mt then
		for i=1,#v do parts[i]=to_json(v[i]) end
		return "["..table.concat(parts,",").."]"
	end
	for k,x in pairs(v) do parts[#parts+1]=to_json(tostring(k))..":"..to_json(x) end
	table.sort(parts);return "{"..table.concat(parts,",").."}"
end
