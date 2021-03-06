#pragma once
#include <unordered_map>



#include "aes.h"
#include "Base64.h"

#define ARIA_THREAD_COUNT 4

struct _Score {

	std::string UserName;
	std::string BeatmapHash;
	int Score;
	DWORD Mods;
	USHORT count300;
	USHORT count100;
	USHORT count50;
	USHORT countGeki;
	USHORT countKatu;
	USHORT countMiss;
	USHORT MaxCombo;
	bool FullCombo;
	std::string Rank;
	byte GameMode;

	float GetAcc() const {
		int TotalHits = count300 + count100 + count50 + countMiss;

		if (TotalHits == 0)return 0.f;

		return float(float(count50 * 50 + count100 * 100 + count300 * 300) / (TotalHits * 300)) * 100;
	}
};

struct _ScoreCache{

	DWORD UserID;
	uint64_t ScoreID;
	int Score;
	int Time;
	DWORD Mods;
	USHORT count300;
	USHORT count100;
	USHORT count50;
	USHORT countGeki;
	USHORT countKatu;
	USHORT countMiss;
	USHORT MaxCombo;
	bool FullCombo;
	byte GameMode;
	float pp;

	float GetAcc() const {
		int TotalHits = count300 + count100 + count50 + countMiss;

		if (TotalHits == 0)return 0.f;

		return float(float(count50 * 50 + count100 * 100 + count300 * 300) / (TotalHits * 300)) * 100;
	}

	_ScoreCache(const _Score &s, const DWORD UID, const float PP){
		ScoreID = 0;
		UserID = UID;
		pp = PP;
		GameMode = s.GameMode;
		Score = s.Score;
		Mods = s.Mods;
		count300 = s.count300;
		count100 = s.count100;
		count50 = s.count50;
		countMiss = s.countMiss;
		countGeki = s.countGeki;
		countKatu = s.countKatu;
		MaxCombo = s.MaxCombo;
		FullCombo = s.FullCombo;
		Time = time(0);
	}

	_ScoreCache() {
		UserID = 0;
		ScoreID = 0;
		GameMode = 0;
		Score = 0;
		Time = 0;
		Mods = 0;
		count300 = 0;
		count100 = 0;
		count50 = 0;
		countGeki = 0;
		countKatu = 0;
		countMiss = 0;
		MaxCombo = 0;
		FullCombo = 0;
		pp = 0;
	}


	//id,score,max_combo,50_count,100_count,300_count,misses_count,katus_count,gekis_count,full_combo,mods,userid,pp,time
	_ScoreCache(sql::ResultSet *r, const byte GM){
		GameMode = GM;
		ScoreID = r->getInt64(1);
		Score = r->getInt(2);
		MaxCombo = r->getInt(3);
		count50 = r->getInt(4);
		count100 = r->getInt(5);
		count300 = r->getInt(6);
		countMiss = r->getInt(7);
		countKatu = r->getInt(8);
		countGeki = r->getInt(9);
		FullCombo = r->getBoolean(10);
		Mods = DWORD(r->getInt(11));
		UserID = r->getInt(12);
		pp = r->getDouble(13);
		Time = r->getInt(14);
	}

};

bool SortScoreCacheByScore(const _ScoreCache i, const _ScoreCache j) { return (i.Score > j.Score); }
bool SortScoreCacheByPP(const _ScoreCache i, const _ScoreCache j) { return (i.pp > j.pp); }


void AppendScoreToString(std::string *s,const DWORD Rank,_ScoreCache& Score, const bool New) {

	if (!s)return;

	const std::string Suffix = New ? "After" : "Before";

	*s += "|rank" + Suffix + ": " + std::to_string(Rank)
	    + "|maxCombo" + Suffix + ": " + std::to_string(Score.MaxCombo)
		+ "|accuracy" + Suffix + ": " + std::to_string(Score.GetAcc())
		+ "|accuracy" + Suffix + ": " + std::to_string(Score.GetAcc())
		+ "|rankedScore" + Suffix + ": " + std::to_string(Score.Score)
		+ "|totalScore" + Suffix + ": " + std::to_string(Score.Score)
		+ "|pp" + Suffix + ": " + std::to_string(Score.pp);
}

struct _LeaderBoardCache{
	DWORD PlayCount;
	DWORD PassCount;

	std::shared_mutex ScoreLock;
	std::vector<_ScoreCache> ScoreCache;

	_LeaderBoardCache() {
		PlayCount = 0;
		PassCount = 0;
	}

	DWORD GetRankByUID(const DWORD UID, const bool NonLock = 0){

		DWORD Rank = 0;

		if(!NonLock)ScoreLock.lock_shared();

		for (DWORD i = 0; i < ScoreCache.size(); i++){
			if (ScoreCache[i].UserID == UID) {
				Rank = i + 1;
				break;
			}
		}
		if (!NonLock)ScoreLock.unlock_shared();

		return Rank;
	}
	_ScoreCache GetScoreByRank(const DWORD Rank){

		_ScoreCache s;

		if (Rank == 0)return s;

		ScoreLock.lock_shared();

		if (Rank <= ScoreCache.size())
			s = ScoreCache[Rank - 1];

		ScoreLock.unlock_shared();

		return s;
	}
	_ScoreCache GetScoreByUID(const DWORD UID) {

		_ScoreCache s;

		if (UID == 0)
			return s;

		ScoreLock.lock_shared();

		for (const auto& SC : ScoreCache) {
			if (SC.UserID == UID) {
				s = SC;
				break;
			}
		}
		ScoreLock.unlock_shared();

		return s;
	}

	bool AddScore(_ScoreCache &s,std::string MD5, _SQLCon *SQL = 0, std::string *Ret = 0){
		
		PlayCount++;
		PassCount++;//dont really care about this getting malformed..

		bool Done = 0;
		bool NewTop = 0;

		MD5 = REMOVEQUOTES(MD5);


		#define TableName std::string(s.Mods & Relax ? "scores_relax" : "scores")

		if (MD5.size() != 32){
			printf("AddScore Incorrect md5 length\n");//should never happen
			return 0;
		}

		ScoreLock.lock();


		int LastRank = 0;
		_ScoreCache LastScore;

		VEC(_SQLKey) ScoreInsert = {
			_SQLKey("beatmap_md5", std::string(MD5)),
			_SQLKey("userid", s.UserID),
			_SQLKey("score", s.Score),
			_SQLKey("max_combo", s.MaxCombo),
			_SQLKey("full_combo", s.FullCombo),
			_SQLKey("mods", s.Mods),
			_SQLKey("300_count", s.count300),
			_SQLKey("100_count", s.count100),
			_SQLKey("50_count", s.count50),
			_SQLKey("katus_count", s.countKatu),
			_SQLKey("gekis_count", s.countGeki),
			_SQLKey("misses_count", s.countMiss),
			_SQLKey("time", s.Time),
			_SQLKey("play_mode", s.GameMode),
			_SQLKey("accuracy", std::to_string(s.GetAcc())),
			_SQLKey("pp", std::to_string(s.pp))
		};

		for (DWORD i = 0; i < ScoreCache.size(); i++){
			if (ScoreCache[i].UserID == s.UserID){
				LastScore = ScoreCache[i];
				Done = 1;
				if (ScoreCache[i].pp < s.pp){

					if (SQL){

						ScoreInsert.push_back(_SQLKey("completed", "3"));

						SQL->Lock.lock();

						SQL->ExecuteUPDATE("UPDATE " + TableName +  " SET completed = 2 WHERE id = " + std::to_string(ScoreCache[i].ScoreID) + " LIMIT 1",1);
						
						SQL->ExecuteUPDATE(SQL_INSERT(TableName,_M(ScoreInsert)),1);

						auto res = SQL->ExecuteQuery("SELECT LAST_INSERT_ID() FROM " + TableName,1);

						SQL->Lock.unlock();
						
						if (res && res->next())
							s.ScoreID = res->getInt64(1);
						
						DeleteAndNull(res);

					}
					ScoreCache[i] = s;
					NewTop = 1;
					LastRank = i + 1;
				}else{
					ScoreInsert.push_back(_SQLKey("completed", "2"));
					SQL->ExecuteUPDATE(SQL_INSERT(TableName, _M(ScoreInsert)));
				}
				break;
			}
		}
		if (!Done){

			ScoreInsert.push_back(_SQLKey("completed", "3"));

			SQL->Lock.lock();

			SQL->ExecuteUPDATE(SQL_INSERT(TableName, _M(ScoreInsert)), 1);

			sql::ResultSet* res = SQL->ExecuteQuery("SELECT LAST_INSERT_ID() FROM " + TableName, 1);

			SQL->Lock.unlock();

			if (res && res->next())
				s.ScoreID = res->getInt64(1);
			if (res)delete res;

			ScoreCache.push_back(s);
			NewTop = 1;
		}
		#undef TableName

		if(NewTop)
			std::sort(ScoreCache.begin(), ScoreCache.end() , (s.Mods & Relax) ? SortScoreCacheByPP : SortScoreCacheByScore);//Could optimize this by bounding it. Will do it if this ever becomes a problem (doubt it)

		const DWORD NewRank = (!NewTop) ? 0 : GetRankByUID(s.UserID,1);
		
		ScoreLock.unlock();

		printf("ScoreID: %llu\n", s.ScoreID);

		if (Ret){
			AppendScoreToString(Ret, LastRank, LastScore, 0);
			AppendScoreToString(Ret, NewRank, s, 1);
			*Ret += "|onlineScoreId: " + std::to_string(s.ScoreID);
		}

		return NewTop;
	}

};
std::shared_mutex SetIDAddMutex[8];



//I know this is sub optimal but ripple did not itterate to include beatmap sets properly
//Which means that a lot of this is hacky just for a single feature (updateable vs un-submited). I would change the ripple API its self but that would require me to edit a lot more.

#define Cache_Pool_Size 8

struct _BeatmapData{
	DWORD BeatmapID;
	DWORD SetID;
	float Rating;
	std::string DisplayTitle;
	std::string DiffName;
	std::string Hash;
	_LeaderBoardCache* lBoard[8];
	char RankStatus;

	_BeatmapData() {
		BeatmapID = 0;
		SetID = 0;
		RankStatus = RankStatus::UNKNOWN;
		Rating = 0.f;
		lBoard[0] = 0;
		lBoard[1] = 0;
		lBoard[2] = 0;
		lBoard[3] = 0;
		lBoard[4] = 0;
		lBoard[5] = 0;
		lBoard[6] = 0;
		lBoard[7] = 0;
	}
	_BeatmapData(const DWORD Status) {
		BeatmapID = 0;
		SetID = 0;
		RankStatus = Status;
		Rating = 0.f;
		lBoard[0] = 0;
		lBoard[1] = 0;
		lBoard[2] = 0;
		lBoard[3] = 0;
		lBoard[4] = 0;
		lBoard[5] = 0;
		lBoard[6] = 0;
		lBoard[7] = 0;
	}

	_LeaderBoardCache* GetLeaderBoard(const DWORD Mode, _SQLCon *SQL){
		if (Mode >= 8)return 0;

		if (lBoard[Mode])
			return lBoard[Mode];
		
		if (SQL){

			const std::string TableName = (Mode >= 4) ? "scores_relax" : "scores";
			const std::string OrderBy = (Mode >= 4) ? "pp" : "score";

			_LeaderBoardCache* L = new _LeaderBoardCache();

			sql::ResultSet *res = SQL->ExecuteQuery("SELECT id,score,max_combo,50_count,100_count,300_count,misses_count,katus_count,gekis_count,full_combo,mods,userid,pp,time FROM " + TableName + " WHERE completed = 3 AND beatmap_md5 = '" + Hash + "' AND play_mode = " + std::to_string(Mode % 4) + " AND pp > 0 ORDER BY "+ OrderBy +" DESC");

			while (res && res->next())
				L->ScoreCache.push_back(_ScoreCache(res, Mode % 4));
			
			if (res)delete res;

			if (!lBoard[Mode])
				lBoard[Mode] = L;
			else delete L;//Another request beat us to it.

			return lBoard[Mode];
		}
		return 0;
	}

	bool AddScore(const DWORD Mode, _ScoreCache &s, _SQLCon *Con, std::string* Ret) {

		if (RankStatus < 2 || s.pp == 0.f)return 0;

		_LeaderBoardCache *L = GetLeaderBoard(Mode, Con);

		if (!L)return 0;

		return L->AddScore(s, Hash, Con,Ret);
	}

};


struct _BeatmapSet{
	DWORD ID;
	DWORD LastUpdate;
	std::shared_mutex Lock;
	std::vector<_BeatmapData> Maps;

	_BeatmapSet(){
		LastUpdate = 0;
		ID = 0;
	}
	_BeatmapSet(DWORD SetID){
		ID = SetID;
		LastUpdate = 0;
	}

};

std::map<const DWORD, _BeatmapSet*> BeatmapSetCache[Cache_Pool_Size];
std::unordered_map<std::string, _BeatmapSet*> BeatmapCache_HASH[Cache_Pool_Size];

std::string GetJsonValue(const std::string &Input, const std::string &&Param) {
	
	DWORD Start = Input.find("\"" + Param + "\":\"");

	if (Start == std::string::npos)
		return "";

	Start += Param.size() + 4;
	DWORD End = Input.size();
	for (DWORD i = Start; i < Input.size(); i++) {
		if (Input[i] == '\"'){
			End = i;
			break;
		}
	}
	return std::string(Input.begin() + Start, Input.begin() + End);
}

int64_t GetJsonValueInt64(const std::string &Input, const std::string &Param) {

	DWORD Start = Input.find("\"" + Param + "\":\"");

	if (Start == std::string::npos)
		return 0;

	Start += Param.size() + 4;
	DWORD End = Input.size();
	for (DWORD i = Start; i < Input.size(); i++) {
		if ((Input[i] < '0' || Input[i] > '9') && Input[i] != '-') {
			End = i;
			break;
		}
	}

	return MemToInt64(&Input[Start],End - Start);
}


std::vector<std::string> JsonListSplit(const std::string& Input) {
	std::vector<std::string> Return;

	if (Input.size() <= 2)return Return;
	
	std::string Current;
	Current.reserve(Input.size());

	int Count = 0;
	bool inQuote = 0;

	for (DWORD i = 1; i < Input.size()-1; i++){
		bool Add = 0;
		if (Input[i] == '{'){

			if (inQuote)
				Add = 1;
			else{
				Count++;
				Add = (Count > 1);
			}
		}
		else if (Input[i] == '}'){

			if (inQuote)
				Add = 1;
			else if (Count > 0){
				Count--;
				if (!Count) {
					Return.push_back(Current);
					Current.clear();
				}

			}
		}
		else if (Count > 0 && Input[i] == '\"') {
			if (Count > 0) {
				inQuote = !inQuote;
				Add = 1;
			}
		}else Add = (Count > 0);

		if(Add)
			Current.push_back(Input[i]);
	}

	if (Current.size())
		Return.push_back(Current);

	return Return;
}

_BeatmapSet BeatmapSetDeleted(0);

std::string ExtractDiffName(const std::string &SRC){

	DWORD Start = 0;
	DWORD End = 0;

	for (DWORD i = SRC.size(); i > 0; i--) {
		if (!End) {
			if (SRC[i - 1] == ']')
				End = i - 1;
		}else{
			if (SRC[i-1] == '[') {
				Start = i;
				break;
			}
		}
	}

	if (!Start || !End)return "";

	return std::string(SRC.begin() + Start, SRC.begin() + End);
}


void FileNameClean(std::string &S){
	std::string Return;
	Return.reserve(S.size());
	
	const char Banned[] = { '/','\\',':','*','?','"', '<','>','|' };
	
	for (DWORD i = 0; i < S.size();i++) {

		bool Add = 1;

		for (DWORD z = 0; z < 9; z++) {
			if (S[i] == Banned[z]) {
				Add = 0;
				break;
			}
		} 

		if (Add)
			Return.push_back(S[i]);
	}
	S = Return;
}

//Calling GetBeatmapSetFromSetID can collect setID information from the osu API if it misses in the database
_BeatmapSet *GetBeatmapSetFromSetID(const DWORD SetID, _SQLCon* SQL, _BeatmapSet* SET = 0 , bool ForceUpdate = 0){
	const char* mName = "Aria";

	if (SetID == 0)return 0;

	const DWORD Off = SetID % Cache_Pool_Size;

	auto MapSet = &BeatmapSetCache[Off];
	if(!SET && !ForceUpdate){

		SHARED_MUTEX_LOCK(SetIDAddMutex[Off]);

		auto it = MapSet->find(SetID);
		if (it != MapSet->end())
			return it->second;
	}

	if (!SQL && !ForceUpdate)
		return 0;

	//Set ID is not in the cache. We need to add it.
		
	SetIDAddMutex[Off].lock();

	//Attempt to get it again. Just incase there was another request that already handling it in the literal nano second of the lock switch
	if(!SET && !ForceUpdate){
		auto it = MapSet->find(SetID);
		if (it != MapSet->end()){
			_BeatmapSet *s = it->second;
			SetIDAddMutex[Off].unlock();
			return s;
		}
	}

	//Still failed. So we have to generate our own Set.
	bool FirstTime = 1;
	sql::ResultSet* res = 0;
TryMap:
	if((!SET && !ForceUpdate) || !FirstTime)
		res = SQL->ExecuteQuery("SELECT ranked,beatmap_id,song_name,rating,beatmap_md5 FROM beatmaps WHERE beatmapset_id = " + std::to_string(SetID));

	if (res && res->next()){
		
		//At least one hit in the database - Cheap and easy. Only expensive thing that could come from this is if the data is out of date. :(

		_BeatmapSet* Set = (!SET) ? new _BeatmapSet(SetID) : SET;//I know using new each time is horrible and will cause memory fragmentation but its the only clean/easy way I can see doing it with the shared_mutex in the strucutre.

		if (SET) {
			Set->Lock.lock();
			Set->Maps.clear();
			Set->Lock.unlock();
		}

		do{
			_BeatmapData l; 
			l.SetID = SetID;
			l.RankStatus = res->getInt(1);
			l.BeatmapID = res->getInt(2);

			l.DisplayTitle = res->getString(3);
			l.DiffName = ExtractDiffName(l.DisplayTitle);
			FileNameClean(l.DiffName);
			ReplaceAll(l.DisplayTitle, "\n", "|");
			
			l.Rating = res->getDouble(4);
			l.Hash = res->getString(5);

			if (l.Hash.size() != 32){
				printf("Malformed MD5 in beatmap table with the set id %i\n", SetID);
				continue;
			}

			Set->Maps.push_back(l);
			BeatmapCache_HASH[(*(DWORD*)&l.Hash[0]) % Cache_Pool_Size].insert(std::pair<std::string, _BeatmapSet*>(l.Hash, Set));

		} while (res && res->next());

		MapSet->insert(std::pair<const DWORD, _BeatmapSet*>(SetID,Set));

		SetIDAddMutex[Off].unlock();

		if (res)delete res;

		return Set;
	}else if(FirstTime){

		//The server does not have the data we need. We need to ask the osu API for all current maps in the set ID and add them to our own database.
		const std::string ApiRes = GET_WEB_CHUNKED("old.ppy.sh", osu_API_BEATMAP + "s=" + std::to_string(SetID));

		if (ApiRes.size() != 0){

			/*DWORD RemovedMapsCount = SQL->ExecuteUPDATE("DELETE FROM beatmaps WHERE beatmapset_id = " + std::to_string(SetID));

			if (RemovedMapsCount){
				const std::string CountString = std::to_string(RemovedMapsCount) + " maps removed.";
				LogMessage(CountString.c_str(), mName);
			}*/

			auto BeatmapData = JsonListSplit(ApiRes);

			if (BeatmapData.size() != 0){

				VEC(DWORD) AddedMaps;
				AddedMaps.reserve(BeatmapData.size());

				for (DWORD i = 0; i < BeatmapData.size(); i++){

					const std::string MD5 = REMOVEQUOTES(GetJsonValue(BeatmapData[i], "file_md5"));
					
					if (MD5.size() == 32){
						const DWORD beatmap_id = StringToUInt32(GetJsonValue(BeatmapData[i], "beatmap_id"));
						if (beatmap_id){
							float diff_size = 0.f;
							float diff_overall = 0.f;
							float diff_approach = 0.f;
							byte mode = 0;
							int Length = 0;
							int RankedStatus = 0;
						
							int MaxCombo = 0;
							int BPM = 0;

							//"C++ needs exceptions!" - noone

							try { diff_size = std::stof(GetJsonValue(BeatmapData[i], "diff_size")); }catch (...) {}
							try { diff_overall = std::stof(GetJsonValue(BeatmapData[i], "diff_overall")); }catch (...) {}
							try { diff_approach = std::stof(GetJsonValue(BeatmapData[i], "diff_approach")); }catch (...) {}
							try { Length = std::stoi(GetJsonValue(BeatmapData[i], "hit_length")); }catch (...) {}
							try { RankedStatus = std::stoi(GetJsonValue(BeatmapData[i], "approved")); }catch (...) {}
							try { MaxCombo = std::stoi(GetJsonValue(BeatmapData[i], "max_combo")); }catch (...) {}
							try { BPM = std::stoi(GetJsonValue(BeatmapData[i], "bpm")); }catch (...) {}

							std::string Title = GetJsonValue(BeatmapData[i], "artist") + " - " + GetJsonValue(BeatmapData[i], "title") + /*" (" + GetJsonValue(BeatmapData[i], "creator") + ")*/" [" + GetJsonValue(BeatmapData[i], "version") + "]";
							FileNameClean(Title);
							ReplaceAll(Title, "'", "''");
							//4 = loved, 3 = qualified, 2 = approved, 1 = ranked, 0 = pending, -1 = WIP, -2 = graveyard
							enum {
								osuapi_graveyard = -2,
								osuapi_WIP = -1,
								osuapi_pending = 0,
								osuapi_ranked = 1,
								osuapi_approved = 2,
								osuapi_qualified = 3,
								osuapi_loved = 4
							};
							
							if (RankedStatus == osuapi_ranked)
								RankedStatus = RANKED;
							else if (RankedStatus == osuapi_qualified)
								RankedStatus = QUALIFIED;

							//TODO calculate diff stars

							auto AlreadyThere = SQL->ExecuteQuery("SELECT id, ranked FROM beatmaps WHERE beatmap_id = " + std::to_string(beatmap_id) + " LIMIT 1");

							if (AlreadyThere && AlreadyThere->next()){

								const int oldStatus = AlreadyThere->getInt(2);

								if (oldStatus == RANKED)
									RankedStatus = RANKED;//Never unrank a beatmap.
								else if (oldStatus == LOVED)
									RankedStatus = LOVED;

								SQL->ExecuteUPDATE(SQL_SETUPDATE("beatmaps", {
								_SQLKey("beatmap_md5",_M(MD5)),
								_SQLKey("song_name",std::move(Title)),
								_SQLKey("ar",std::to_string(diff_approach)),
								_SQLKey("od",std::to_string(diff_overall)),
								_SQLKey("max_combo",MaxCombo),
								_SQLKey("hit_length",Length),
								_SQLKey("bpm",BPM),
								_SQLKey("ranked",RankedStatus)
								}, std::string("id =" + AlreadyThere->getString(1) + " LIMIT 1")));


							}else SQL->ExecuteUPDATE(SQL_INSERT("beatmaps", {
								_SQLKey("beatmap_id", beatmap_id),
								_SQLKey("beatmapset_id", SetID),
								_SQLKey("beatmap_md5", _M(MD5)),
								_SQLKey("song_name", std::move(Title)),
								_SQLKey("ar", std::to_string(diff_approach)),
								_SQLKey("od", std::to_string(diff_overall)),
								_SQLKey("difficulty_std", 0),
								_SQLKey("difficulty_taiko", 0),
								_SQLKey("difficulty_ctb", 0),
								_SQLKey("difficulty_mania", 0),
								_SQLKey("max_combo", MaxCombo),
								_SQLKey("hit_length",Length),
								_SQLKey("bpm", BPM),
								_SQLKey("ranked", RankedStatus),
								_SQLKey("latest_update", "0"),
								_SQLKey("ranked_status_freezed", "0")
							}));

							AddedMaps.push_back(beatmap_id);
						}

					}
				}

				{
					std::string NewMaps;

					for (const DWORD bID : AddedMaps)
						NewMaps += std::to_string(bID) + ",";
					
					int DeletedDiffs = 0;
					if (NewMaps.size()) {
						NewMaps.pop_back();
						
						DeletedDiffs = SQL->ExecuteUPDATE("DELETE FROM beatmaps WHERE beatmapset_id = " + std::to_string(SetID) + " AND beatmap_id NOT IN ("+ NewMaps +")");

					}else DeletedDiffs = SQL->ExecuteUPDATE("DELETE FROM beatmaps WHERE beatmapset_id = " + std::to_string(SetID));


					const std::string CountString = std::to_string(DeletedDiffs) + " diffs removed.";
					LogMessage(CountString.c_str(), mName);


				}
				LogMessage("Maps done", mName);
				if (res)delete res;
				LogMessage("Got data. Attempting grab again.", mName);
				FirstTime = 0;
				
				goto TryMap;
			}else{//TODO: Disallow people from spamming future set ids that do not exist thus locking them.

				LogMessage("SetID has been removed from the osu server\n", mName);

				{//The set does not exist on the osu server. Cache this fact.
					auto it = MapSet->find(SetID);

					if(it == MapSet->end())
						MapSet->insert(std::pair<const DWORD, _BeatmapSet*>(SetID, &BeatmapSetDeleted));
					else (*it).second->ID = 0;
				}
			}
		}else{
			LogMessage("Could not connect to the osu!API", mName);
		}
	}

	SetIDAddMutex[Off].unlock();

	if (res)delete res;

	return 0;
}
_BeatmapData BeatmapNotSubmitted(RankStatus::UNKNOWN);


bool CheckMapUpdate(_BeatmapData *BD, _SQLCon* SQL) {

	if (!BD || !BD->SetID || BD->Hash.size() != 32 || !SQL)
		return 0;
	
	const DWORD Off = BD->SetID % Cache_Pool_Size;

	_BeatmapSet *BS = 0;

	SetIDAddMutex[Off].lock_shared();

	{
		const DWORD pOff = (*(DWORD*)&BD->Hash[0]) % Cache_Pool_Size;

		auto it = BeatmapCache_HASH[pOff].find(BD->Hash);

		if (it != BeatmapCache_HASH[pOff].end())//This is most likely not thread safe.
			BS = it->second;
	}

	SetIDAddMutex[Off].unlock_shared();
	
	if (!BS)
		return 0;

	const DWORD cTime = clock_ms();

	if (BS->LastUpdate + 14400000 > cTime)
		return 0;
	BS->LastUpdate = cTime;
	GetBeatmapSetFromSetID(BD->SetID, SQL, BS, 1);
	return 1;
}


_BeatmapData* GetBeatmapCache(const DWORD SetID, const DWORD BID,const std::string &MD5, const std::string &&DiffName, _SQLCon* SQL){

	//if (GameMode >= 8)return 0;

	bool ValidMD5 = (MD5.size() == 32);
	bool DiffNameGiven = (DiffName.size() > 0);
	_BeatmapSet *BS = 0;

	if (ValidMD5 && SetID) {
		const DWORD pOff = (*(DWORD*)&MD5[0]) % Cache_Pool_Size;

		const DWORD Off = SetID % Cache_Pool_Size;

		SetIDAddMutex[Off].lock_shared();

		{

			auto it = BeatmapCache_HASH[pOff].find(MD5);

			if (it != BeatmapCache_HASH[pOff].end())//This is most likely not thread safe.
				BS = it->second;
		}

		SetIDAddMutex[Off].unlock_shared();

	}

	if (BS == &BeatmapSetDeleted)
		return 0;

	if(!BS)
		BS = GetBeatmapSetFromSetID((SetID) ? SetID : getSetID_fHash(MD5, SQL), SQL);

	if (BS){

	CheckSet:

		if(!BS->ID)
			return &BeatmapNotSubmitted;
		{
			SHARED_MUTEX_LOCK(BS->Lock);

			for (auto& Map : BS->Maps) {

				if ((ValidMD5 && Map.Hash == MD5) || 
					(DiffNameGiven && Map.DiffName == DiffName))//TODO: check if the servers md5 is out of date.
					return &Map;//This feels like returning a reference to a local.
			}

		}
		const DWORD cTime = clock_ms();

		if (BS->LastUpdate + 14400000 < cTime){//Rate limit to every 4 hours
			BS->LastUpdate = cTime;

			LogMessage("Possible update to beatmap set.", "Aria");
			
			BS = GetBeatmapSetFromSetID(SetID, SQL, BS);
			if (BS)goto CheckSet;

		}

	}else printf("Could not find the map at all.\n");

	//Worst case scenario. The beatmap is not in the database and the osuapi can not be reached.

	printf("Map Fail\n");

	return 0;
}


std::mutex AriaThreadLock[ARIA_THREAD_COUNT];
_SQLCon AriaSQL[ARIA_THREAD_COUNT];
std::vector<_Con> AriaConnectionQue[ARIA_THREAD_COUNT];

std::string unAesString(const std::string &Input, const std::string &K, const std::string &IV) {

	if (!Input.size() || !IV.size() || !K.size())return "";

	char Key[32];
	char Iv[32];
	ZeroMemory(Key, 32);
	ZeroMemory(Iv, 32);

	memcpy(Key, &K[0], (32 <= K.size()) ? 32 : K.size());
	memcpy(Iv, &IV[0], (32 <= IV.size()) ? 32 : IV.size());

	std::string rString(Input.size(), '\0');

	try {
		CRijndael rj;
		rj.MakeKey(Key, Iv, 32, 32);
		rj.Decrypt((char*)&Input[0], (char*)&rString[0], Input.size());
	}
	catch(...){
		printf(KRED "AES FAILED\n" KRESET);
		return "";
	}

	return rString;
}

enum scoreOffset {
	score_FileCheckSum,
	score_PlayerName,
	score_onlineCheckSum,
	score_Count300,
	score_Count100,
	score_Count50,
	score_CountGeki,
	score_CountKatu,
	score_CountMiss,
	score_totalScore,
	score_maxCombo,
	score_Perfect,
	score_Rank,
	score_Mods,
	score_Pass,
	score_playMode,
	score_Date,
	Score_Version
};

__forceinline std::string GetParam(const std::string &s, const std::string param) {

	DWORD Off = s.find(param);
	if (Off == std::string::npos)return "";
	Off += param.size();

	DWORD End = s.size();

	for (DWORD i = Off + 1; i < s.size(); i++) {
		if (s[i] == '&') {
			End = i;
			break;
		}
	}

	return std::string(s.begin() + Off, s.begin() + End);
}

void SendAria404(_Con s){

	s.SendData(ConstructResponse(200, Empty_Headers, FastVByteAlloc("<HTML><img src=\"https://cdn.discordapp.com/attachments/385279293007200258/570910676428652544/Kanzaki.png\"><br><b>404</b> - Aria does not know this page.</HTML>")));
	s.Dis();
}

void TryScoreAgain(_Con s){
	s.SendData(ConstructResponse(408, Empty_Headers, Empty_Byte));
	s.Dis();
}
void ScoreFailed(_Con s){
	s.SendData(ConstructResponse(408, Empty_Headers, FastVByteAlloc("error: no")));
	s.Dis();
}

_Achievement GetAchievementsFromScore(const _Score &s, const float StarDiff) {

	_Achievement Ret;
		
	DWORD *const Gen = [&](){
		if (s.GameMode == 0)
			return &Ret.osuGeneral;
		else if (s.GameMode == 1)
			return &Ret.taikoGeneral;
		else if (s.GameMode == 2)
			return &Ret.ctbGeneral;
		return &Ret.maniaGeneral;
	}();

	if (s.MaxCombo >= 500)
		*Gen += AchGeneral::Combo500;
	if (s.MaxCombo >= 750)
		*Gen += AchGeneral::Combo750;
	if (s.MaxCombo >= 1000)
		*Gen += AchGeneral::Combo1000;
	if (s.MaxCombo >= 2000)
		*Gen += AchGeneral::Combo2000;

	int StarCount = al_min(int(StarDiff), 8);

	while (StarCount > 0){
		if (s.FullCombo)
			*Gen += 1 << (11 + StarCount);
		*Gen += 1 << (3 + StarCount);

		StarCount--;
	}

	return Ret;
}

const USHORT RCNL = *(USHORT*)"\r\n";



void ScoreServerHandle(const _HttpRes &res, _Con s){
	

	/*std::chrono::steady_clock::time_point sTime = std::chrono::steady_clock::now();
	const unsigned long long TTime = std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - sTime).count();
	printf("%fms\n", double(double(TTime) / 1000000.0));*/

	const char* mName = "Aria";

	//LogMessage("Start reading score.", mName);

	int FailTime = -1;
	int Quit = -1;

	std::string score,
				bmk,//Beatmap hash - used to detect changing the map as it is loading.
				c1,
				pass,
				osuver,
				clienthash,
				Image,//Used if osu thinks the person is flash light cheating.
				iv,ReplayFile;

	const auto RawScoreData = EXPLODE_MULTI(std::string, &res.Body[0], res.Body.size(), "-------------------------------28947758029299\r\n");

	if (RawScoreData.size() < 10){

		printf("RawScoreData was under 10(%i)\n",RawScoreData.size());

		return TryScoreAgain(s);
	}else {
		bool FirstScoreParam = 1;
		for (DWORD i = 0; i < RawScoreData.size(); i++){

			if (!MEM_CMP_START(RawScoreData[i], "Content-Disposition: form-data; name=\""))
				continue;

			const char* End = &RawScoreData[i][38];
			const char* Start = &RawScoreData[i][38];
			const size_t DataSize = RawScoreData[i].size() - 39;

			if (DataSize == size_t(-1) || !DataSize)
				continue;

			bool QuickExit = 0;

			while (1) {
				End++;
				if (*End == '"')break;
				if (size_t(End - Start) >= DataSize) {
					QuickExit = 1;
					break;
				}
			}if (QuickExit)continue;

			const std::string Name(Start, End);

			Start = End + 5;
			End = &RawScoreData[i][RawScoreData[i].size() - 2];
			if (Start >= End)continue;

			if (Name == "ft")
				FailTime = MemToInt32(Start, DWORD(End - Start));
			else if (Name == "x")
				Quit = MemToInt32(Start, DWORD(End - Start));
			else if (Name == "iv")
				iv = std::string(Start, End);
			else if (Name == "score"){
				if(FirstScoreParam)
					score = std::string(Start, End);
				else {
					Start += 58;
					End -= _strlen_("-------------------------------28947758029299--");
					if (Start >= End)continue;
					ReplayFile = std::string(Start, End);
				}
				FirstScoreParam = 0;
			}else if (Name == "bmk")
				bmk = std::string(Start, End);
			else if (Name == "c1")
				c1 = std::string(Start, End);
			else if (Name == "bmk")
				bmk = std::string(Start, End);
			else if (Name == "pass")
				pass = std::string(Start, End);
			else if (Name == "osuver")
				osuver = std::string(Start, End);
			else if (Name == "s")
				clienthash = std::string(Start, End);
			else if (Name == "i")
				Image = std::string(Start, End);

		}

		if (!iv.size() || !clienthash.size() || !osuver.size() || !score.size() || pass.size() != 32){//something very important is missing.
				LogError("Failed score.","Aria");
				return TryScoreAgain(s);
		}
		const bool OldClient = (FailTime == -1 || Quit == -1);

		if (FailTime == -1)
			FailTime = 0;
		if (Quit == -1)
			Quit = 0;



		iv = base64_decode(iv);
		
		const std::string Key = "osu!-scoreburgr---------" + osuver;

		_Score sData;
		const auto ScoreData = EXPLODE_VEC(std::string, unAesString(base64_decode(score), Key, iv),':');

		if (ScoreData.size() != 18){
			const std::string Error = "Score sent with wrong ScoreData length (" + std::to_string(ScoreData.size()) + ")\0";
			LogError(&Error[0], "Aria");
			return ScoreFailed(s);
		}

		sData.BeatmapHash = REMOVEQUOTES(ScoreData[scoreOffset::score_FileCheckSum]);

		sData.UserName = ScoreData[scoreOffset::score_PlayerName];
		sData.count300 = StringToInt32(ScoreData[score_Count300]);
		sData.count100 = StringToInt32(ScoreData[score_Count100]);
		sData.count50 = StringToInt32(ScoreData[score_Count50]);
		sData.countGeki = StringToInt32(ScoreData[score_CountGeki]);
		sData.countKatu = StringToInt32(ScoreData[score_CountKatu]);
		sData.countMiss = StringToInt32(ScoreData[score_CountMiss]);
		sData.Score = StringToUInt32(ScoreData[score_totalScore]);
		sData.MaxCombo = StringToInt32(ScoreData[score_maxCombo]);
		sData.FullCombo = (ScoreData[score_Perfect] == "True") ? 1 : 0;
		sData.GameMode = StringToInt32(ScoreData[score_playMode]);
		sData.Mods = StringToUInt32(ScoreData[score_Mods]);
		//Score Data is ready to read.

		if (sData.UserName.size() && sData.UserName[sData.UserName.size()-1] == ' ')
			sData.UserName.pop_back();//Pops off supporter client check.

		_UserRef u(GetUserFromNameSafe(USERNAMESAFE(sData.UserName)),1);

		if (!u.User || !u.User->choToken){
			//They might still be logging in. Just abort the connection to let their client (hopefully) attempt again.
			//Hopefully once they have an active ruri connection.
			printf("%s> is not online?\n", sData.UserName.c_str());
			return TryScoreAgain(s);
		}
		u.User->Stats[(sData.Mods & Relax ? sData.GameMode + 4 : sData.GameMode) % 8].PlayCount++;
		const DWORD UserID = u.User->UserID;
		if (!MD5CMP(u.User->Password, &pass[0])){
			printf("%s> password wrong\n", sData.UserName.c_str());
			return TryScoreAgain(s);
		}		

		if (u.User->privileges & UserPublic && !FailTime && !Quit){
			byte lGameMode = sData.GameMode;

			if (sData.Mods & Relax)lGameMode += 4;

			if (lGameMode >= 8)lGameMode = 0;

			_BeatmapData *BD = GetBeatmapCache(0, 0, sData.BeatmapHash, "", &AriaSQL[s.ID]);

			if (!BD) {
				printf(KRED"(%s) ScoreSubmit map failure\n" KRESET,sData.BeatmapHash.c_str());
				return TryScoreAgain(s);
			}
			float PP = 0.f;
			float MapStars = 0.f;
			if (BD->RankStatus == RANKED){
				ezpp_t ez = ezpp_new();

				if (!ez) {
					LogError("Failed to load ezpp" "Aria");
					return TryScoreAgain(s);
				}

				ezpp_set_mods(ez, sData.Mods);
				ezpp_set_nmiss(ez, sData.countMiss);
				ezpp_set_accuracy(ez, sData.count100, sData.count50);
				ezpp_set_combo(ez, sData.MaxCombo);
				if (!OppaiCheckMapDownload(ez, BD->BeatmapID)){
					printf("Could not download\n");
					return TryScoreAgain(s);
				}
				PP = ezpp_pp(ez);
				MapStars = (sData.Mods & (NoFail | Relax | Relax2)) ? 0.f : ezpp_stars(ez);
			}
			_ScoreCache sc(sData,u.User->UserID,PP);
			
			std::string ClientScoreUpdate;

			bool NewBest = BD->AddScore(lGameMode, sc, &AriaSQL[s.ID],&ClientScoreUpdate);

			std::string Charts =
				"beatmapId: " + std::to_string(BD->BeatmapID) +
				"|beatmapSetId: " + std::to_string(BD->SetID) +
				"|beatmapPlaycount: 1"
				"|beatmapPasscount: 1"
				"|approvedDate: 0\n";

			if (ClientScoreUpdate.size() != 0){
				Charts += "chartId: beatmap"
					"|chartUrl: https://osu.ppy.sh/b/" + std::to_string(BD->BeatmapID) +
					"|chartName: Beatmap Ranking" + ClientScoreUpdate;
				
				//TODO: might want to add overall stats
				std::string achievements;
				
				_Achievement New = GetAchievementsFromScore(sData, MapStars);

				CalculateAchievement(New, u.User->Ach, sData.GameMode, &achievements);//TODO Add ach to DB.
				u.User->Ach = New;
				if(achievements.size())
					Charts += "|achievements-new: " + achievements + "\n";
				else Charts += "\n";
			}
			
			s.SendData(ConstructResponse(200, Empty_Headers, std::vector<byte>(Charts.begin(), Charts.end())));
			s.Dis();

			if(NewBest && UpdateUserStatsFromDB(&AriaSQL[s.ID], UserID, lGameMode, u.User->Stats[lGameMode]))
				u.User->addQue(bPacket::UserStats(u.User));

			if (NewBest && sc.ScoreID && ReplayFile.size()){
				//Might want to save the headers into the file its self.
				//The only time having raw data would be nice is when someone changes their username. But is it really that big of an issue. Could leave the name as the userid and only resolve that (with the name cache) on fetch.
				WriteAllBytes(REPLAY_PATH +std::to_string(sc.ScoreID) + ".osr", ReplayFile);
			}

			return;
		}

		s.SendData(ConstructResponse(200, Empty_Headers, FastVByteAlloc("error: no")));
		s.Dis();

		AriaSQL[s.ID].ExecuteUPDATE(SQL_INSERT(sData.Mods & Relax ? "scores_relax" : "scores",
		{
		_SQLKey("beatmap_md5",std::string(sData.BeatmapHash)),
		_SQLKey("userid",UserID),
		_SQLKey("score",sData.Score),
		_SQLKey("max_combo",sData.MaxCombo),
		_SQLKey("full_combo",sData.FullCombo),
		_SQLKey("mods",sData.Mods),
		_SQLKey("300_count",sData.count300),
		_SQLKey("100_count",sData.count100),
		_SQLKey("50_count",sData.count50),
		_SQLKey("katus_count",sData.countKatu),
		_SQLKey("gekis_count",sData.countGeki),
		_SQLKey("misses_count",sData.countMiss),
		_SQLKey("time",time(0)),
		_SQLKey("play_mode",sData.GameMode),
		_SQLKey("completed",0),
		_SQLKey("accuracy",std::to_string(sData.GetAcc())),
		_SQLKey("pp",0)
		}));

		return;
	}

}

enum RankingType
{
	Local,
	Top,
	SelectedMod,
	Friends,
	Country
};

std::string urlDecode(const std::string &SRC) {
	std::string ret;
	ret.reserve(SRC.size());
	int i, ii;
	for (i = 0; i<SRC.length(); i++) {
		if (SRC[i] == '%') {
			sscanf(SRC.substr(i + 1, 2).c_str(), "%x", &ii);
			ret.push_back(char(ii));
			i = i + 2;
		}
		else {
			ret += SRC[i];
		}
	}
	ReplaceAll(ret, "+", " ");

	return ret;
}

void osu_getScores(const _HttpRes &http, _Con s){
	const char* mName = "Aria";
	const std::string URL(http.Host.begin(), http.Host.end());

	const std::string BeatmapMD5 = REMOVEQUOTES(GetParam(URL, "&c="));

	const DWORD SetID = StringToUInt32(GetParam(URL,"&i="));

	if (BeatmapMD5.size() != 32)
		return SendAria404(s);

	const std::string UserName = urlDecode(GetParam(URL,"&us="));
	const std::string Password = GetParam(URL, "&ha=");
	//Might want to lock the user or something in the future. just incase

	_UserRef u(GetUserFromName(UserName),1);
	if (!u.User || Password.size() != 32 || !MD5CMP(&Password[0],u.User->Password))
		return SendAria404(s);

	const DWORD Mods = StringToUInt32(GetParam(URL, "&mods="));
	DWORD Mode = StringToUInt32(GetParam(URL, "&m="));
	const bool CustomClient = (URL.find("&vv=4") == std::string::npos);
	const DWORD LType = StringToUInt32(GetParam(URL,"&v="));
	const std::string ScoreTableName = (Mods & Relax) ? "scores_relax" : "scores";
	const std::string DiffName = ExtractDiffName(urlDecode(GetParam(URL, "&f=")));
	if ((u.User->actionMods & Relax) != (Mods & Relax)){//actionMods is outdated.
		u.User->actionMods = Mods;
		u.User->addQue(bPacket::UserStats(u.User));//Send the updates stats back to the client.
	}
	
	if (u.User->actionID != bStatus::sPlaying && BeatmapMD5.size() == 32)
		memcpy(&u.User->ActionMD5[0], &BeatmapMD5[0], 32);

	if (Mods & Relax)
		Mode += 4;

	if (Mode >= 8)
		Mode = 0;

	_BeatmapData*const BeatData = GetBeatmapCache(SetID,0, BeatmapMD5, std::move(DiffName),&AriaSQL[s.ID]);

	if (!BeatData || !BeatData->BeatmapID){

		char Status = RankStatus::NOT_SUBMITTED;

		if (!BeatData) {
			LogError(std::string("Failed to get the beatmap with md5 " + BeatmapMD5).c_str(), "Aria");
		}

		std::string BeatmapFailed = std::to_string(Status) + "|0";

		s.SendData(ConstructResponse(200, Empty_Headers, std::vector<byte>(BeatmapFailed.begin(), BeatmapFailed.end())));
		return s.Dis();
	}


	const bool NeedUpdate = (BeatData && BeatData->Hash != BeatmapMD5);

	if (NeedUpdate)
		CheckMapUpdate(BeatData, &AriaSQL[s.ID]);//TODO: Should probably properly handle this instead of this hack.


	_LeaderBoardCache *const LeaderBoard = BeatData->GetLeaderBoard(Mode, &AriaSQL[s.ID]);

	const DWORD TotalScores = (LeaderBoard) ? LeaderBoard->ScoreCache.size() : 0;
	
	std::string Response = (NeedUpdate) ? "1" : std::to_string(BeatData->RankStatus);
	Response += "|false"//server osz
				"|" + std::to_string(BeatData->BeatmapID)//beatmap id
			  + "|" + std::to_string(BeatData->SetID)//set id
	          + "|" + std::to_string(TotalScores)//total records
			  + "\n0"//offset
				"\n" + BeatData->DisplayTitle//song name
			  + "\n" + std::to_string(BeatData->Rating) + "\n";//rating
	//Personal best
	if (!NeedUpdate && BeatData->RankStatus >= RANKED && LeaderBoard){
		DWORD Rank = LeaderBoard->GetRankByUID(u.User->UserID);

		if (Rank){

			_ScoreCache s = LeaderBoard->GetScoreByRank(Rank);

			if (s.UserID != u.User->UserID)
				s = LeaderBoard->GetScoreByUID(u.User->UserID);
			if (s.UserID != 0) {
				Response += std::to_string(s.ScoreID);//online id
				Response += "|" + u.User->Username;//player name
				Response += "|" + std::to_string((Mode>=4) ? int(s.pp + 0.5f) : s.Score);//total score
				Response += "|" + std::to_string(s.MaxCombo);//max combo
				Response += "|" + std::to_string(s.count50);//count 50
				Response += "|" + std::to_string(s.count100);//count 100
				Response += "|" + std::to_string(s.count300);//count 300
				Response += "|" + std::to_string(s.countMiss);//count miss
				Response += "|" + std::to_string(s.countKatu);//count katu
				Response += "|" + std::to_string(s.countGeki);//count geki
				if (s.FullCombo)Response += "|1";//perfect
				else Response += "|0";//perfect
				Response += "|" + std::to_string(s.Mods);//mods
				Response += "|" + std::to_string(u.User->UserID);//userid
				Response += "|" + std::to_string(Rank);//online rank
				Response += "|" + std::to_string(s.Time);//date 
				Response += "|1";//online replay
			}
		}


		if (LeaderBoard->ScoreCache.size()) {

			LeaderBoard->ScoreLock.lock_shared();

			DWORD Rank = 0;

			for (DWORD i = 0; i < LeaderBoard->ScoreCache.size(); i++) {

				if (Rank >= 1000)
					break;//Not doing this for performance reasons of the server. Simply the client is not built to handle it.

				const _ScoreCache *lScore = &LeaderBoard->ScoreCache[i];
				
				if (LType == RankingType::SelectedMod && lScore->Mods != Mods)
					continue;

				if (LType == RankingType::Friends && !u.User->isFriend(lScore->UserID))
					continue;

				Rank++;

				if (u.User->isBlocked(lScore->UserID))
					continue;

				Response += "\n" + std::to_string(lScore->ScoreID)//online id
				         + "|" + GetUsernameFromCache(lScore->UserID)//player name
				         + "|" + std::to_string((Mode >= 4) ? int(lScore->pp + 0.5f) : lScore->Score)//total score
						 + "|" + std::to_string(lScore->MaxCombo)//max combo
						 + "|" + std::to_string(lScore->count50)//count 50
						 + "|" + std::to_string(lScore->count100)//count 100
						 + "|" + std::to_string(lScore->count300)//count 300
						 + "|" + std::to_string(lScore->countMiss)//count miss
						 + "|" + std::to_string(lScore->countKatu)//count katu
						 + "|" + std::to_string(lScore->countGeki)//count geki
						 + std::string((lScore->FullCombo) ? "|1" : "|0")
						 + "|" + std::to_string(lScore->Mods)//mods
						 + "|" + std::to_string(lScore->UserID)//userid
						 + "|" + std::to_string(Rank)//online rank
						 + "|" + std::to_string(lScore->Time)//date
						 + "|1";//online replay
			}

			LeaderBoard->ScoreLock.unlock_shared();
		}
	}

	s.SendData(ConstructResponse(200, Empty_Headers, std::vector<byte>(Response.begin(),Response.end())));
	s.Dis();
}

__forceinline const bool SafeStartCMP(const std::vector<byte> &b, const std::string &&Check){
	if (b.size() < Check.size())return 0;
	return (memcmp(&b[0], &Check[0], Check.size()) == 0);
}

struct _UpdateCache{
	int Stream;
	DWORD LastTime;
	std::vector<byte> Cache;
	_UpdateCache() {
		Stream = 0;
		LastTime = 0;
	}
}; std::vector<_UpdateCache> UpdateCache;

void osu_checkUpdates(const std::vector<byte> &Req,_Con s){

	if (!SafeStartCMP(Req, "/web/check-updates.php?action=check") && !SafeStartCMP(Req, "/web/check-updates.php?action=path"))
		return SendAria404(s);

	const std::string URL(Req.begin() + 1,Req.end());
	int CacheOffset = -1;
	
	const int Stream = WeakStringToInt(GetParam(URL, "stream="));
	
	for (DWORD i = 0; i < UpdateCache.size(); i++){

		if (UpdateCache[i].Stream == Stream){

			if (UpdateCache[i].LastTime + 3600000 > clock_ms()){
				s.SendData(ConstructResponse(200, Empty_Headers,UpdateCache[i].Cache));
				return s.Dis();
			}

			CacheOffset = i;
			break;
		}

	}

	const std::string res = GET_WEB_CHUNKED("old.ppy.sh",URL);

	s.SendData(ConstructResponse(200, Empty_Headers, std::vector<byte>(res.begin(), res.end())));
	s.Dis();

	_UpdateCache c;

	c.Stream = Stream;
	c.LastTime = clock_ms();
	c.Cache = std::vector<byte>(res.begin(),res.end());

	if (CacheOffset == -1)
		UpdateCache.push_back(c);
	else UpdateCache[CacheOffset] = c;
}
/*
void Thread_Handle_SearchSet(_Con s){

	DWORD Start = 0;

	for (DWORD i = 19; i < http.Host.size() - 1; i++) {
		if (http.Host[i] == '&'  && http.Host[i+1] != 'u' && http.Host[i+1] != 'h') {
			Start = i + 1;
			break;
		}
	}

	if (!Start)
		return s.Dis();

	std::string Res;// GetMirrorResponse("api/set?" + std::string(http.Host.begin() + Start, http.Host.end()));

	//UnChunk(Res);

	if (!Res.size())
		return s.Dis();

	s.SendData(ConstructResponse(200, Empty_Headers, std::vector<byte>(Res.begin(), Res.end())));
	return s.Dis();
}*/

const std::string directToApiStatus(const std::string &directStatus) {//thank you ripple
	if (!directStatus.size())
		return "";
	if (directStatus == "0" || directStatus == "7")
		return "1";
	if(directStatus == "8")
		return "4";
	if (directStatus == "3")
		return "3";
	if (directStatus == "2")
		return "0";
	if (directStatus == "5")
		return "-2";
	if (directStatus == "4")
		return "";

	return "1";
}

void Thread_Handle_DirectSearch(const std::string URL, _Con s){
	
	const std::string r = GetParam(URL, "?r=");//Yes today I am ripple and im going to change the names for no reason. Yes.
	const std::string q = GetParam(URL, "&q=");
	//const std::string m = GetParam(URL, "&m=");
	std::string Res = GET_WEB_CHUNKED(MIRROR_IP, "api/search?query=" + q + "&offset=" + std::to_string(StringToInt32(GetParam(URL, "&p=")) * 50) + "&status=" + directToApiStatus(r));

	if (Res.size() == 0){

		//std::string MirrorFailure = "1\n | |Could not contact the mirror|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0";

		s.SendData(ConstructResponse(200, Empty_Headers, Empty_Byte));

		return s.Dis();
	}

	//const auto JSON = JsonListSplit(Res);//Imagine being surrounded by people who dont know the own code they are running to the point where they do not even know how to NOT return JSON.

	std::string Return = "1069";

	for (const auto& JSON : JsonListSplit(Res)){

		Return += "\n";
		const DWORD SetID = GetJsonValueInt64(JSON, "SetID");
		const char RankedStatus = GetJsonValueInt64(JSON, "RankedStatus");
		const std::string ApprovedDate = GetJsonValue(JSON, "ApprovedDate");
		const std::string LastUpdate = GetJsonValue(JSON, "LastUpdate");
		const std::string LastChecked = GetJsonValue(JSON, "LastChecked");
		const std::string Artist = GetJsonValue(JSON, "Artist");
		const std::string Title = GetJsonValue(JSON, "Title");
		const std::string Creator = GetJsonValue(JSON, "Creator");
		const std::string Source = GetJsonValue(JSON, "Source");
		const std::string Tags = GetJsonValue(JSON, "Tags");
		//HasVideo
		const char Genre = GetJsonValueInt64(JSON, "Genre");
		const char Language = GetJsonValueInt64(JSON, "Language");
		const char Favourites = GetJsonValueInt64(JSON, "Favourites");


		Return += std::to_string(SetID) + ".osz|" + Artist + "|" + Title + "|" + Creator + "|" + std::string(1, RankedStatus) + "|0.00|" + LastUpdate + "|" + std::to_string(SetID) +
			"|" + std::to_string(SetID) + "|0|0|1337||blame maxi@0";


		//const auto Diffs = JsonListSplit(JSON[i]);

		Return += "|";
	}


	s.SendData(ConstructResponse(200, Empty_Headers, std::vector<byte>(Return.begin(), Return.end())));

	return s.Dis();
}

void Thread_GetReplay(const std::string URL, _Con s) {

	const uint64_t ScoreID = StringToUInt64(GetParam(URL, "&c="));
	if (!ScoreID)
		return s.Dis();

	std::vector<byte> Data = LOAD_FILE(REPLAY_PATH + std::to_string(ScoreID) + ".osr");

	if (!Data.size())
		return s.Dis();

	Data.pop_back();

	s.SendData(ConstructResponse(200, Empty_Headers, Data));
	
	return s.Dis();
	/*
	_SQLCon sql;

	if (!sql.Connect())
		return s.close();

	//TODO: decide what to do about the **disgusting** legacy solution to relax.

	sql::ResultSet *res = sql.ExecuteQuery("SELECT userid, beatmap_md5, score, max_combo, full_combo, mods, 300_count, 100_count, 50_count, "
		"katus_count, gekis_count, misses_count, time, play_mode from scores WHERE id = " + std::to_string(ScoreID));

	if (!res || !res->next()) {
		if (res)delete res;
		return s.close();
	}*/

	//beatmap_md5, score, max_combo, full_combo, mods, 300_count, 100_count, 50_count,
	// katus_count, gekis_count, misses_count, time, play_mode
}

void Thread_DownloadOSZ(const std::string URL, _Con s){

	const DWORD DataOff = URL.find('?');

	const DWORD MapID = MemToUInt32(&URL[0],((DataOff == std::string::npos) ? URL.size() : DataOff));

	const std::string Res = GET_WEB(MIRROR_IP, "d/" + std::to_string(MapID));

	if (Res.find("\r\n\r\n") == std::string::npos || Res.size() < 150){
		s.SendData(ConstructResponse(404, Empty_Headers, Empty_Byte));

		const std::string Username = GetParam(URL, "?u=");
		const std::string Password = GetParam(URL, "&h=");

		if (Username.size() && Password.size() == 32) {

			_UserRef u(GetUserFromNameSafe(USERNAMESAFE(Username)),1);

			if (u.User && MD5CMP(u.User->Password, &Password[0]))
				u->addQue(bPacket::Notification(std::to_string(MapID) + " is not on the mirror sorry! There is nothing ruri can do :("));
		}


		return s.Dis();
	}
	
	s.SendData(std::move(Res));
	
	return s.Dis();
}

void Thread_UpdateOSU(const std::string URL, _Con s) {
	
	/*
	std::string FileName(http.Host.begin() + _strlen_("/web/maps/"), http.Host.end() - _strlen_(".osu"));

	if (FileName.size() < 4)
		return s.Dis();

	ReplaceAll(FileName, "'", "''");
	FileName = urlDecode(FileName);

	auto res = AriaSQL[s.ID].ExecuteQuery("SELECT beatmap_id FROM beatmaps WHERE song_name = '" + FileName + "' LIMIT 1");

	DWORD ID = 0;

	if (res && res->next())
		ID = res->getUInt(1);
	DeleteAndNull(res);

	printf("%s:%i\n",FileName.c_str(), ID);

	if (!ID)
		return s.Dis();*/

	s.SendData(GET_WEB("old.ppy.sh", _M(URL)));//Their osu clients will unchunk the data for us :)

	return s.Dis();
}

void HandleAria(_Con s){

	const char* mName = "Aria";

	_HttpRes res;

	if (!s.RecvData(res)){
		s.Dis();
		return LogError("Connection Lost", mName);
	}

	bool DontCloseConnection = 0;
	if (SafeStartCMP(res.Host, "/web/osu-submit-modular-selector.php")){

		const int sTime = clock_ms();

		ScoreServerHandle(res, s);

		printf("Score Done in %ims\n", clock_ms() - sTime);

	}else if (SafeStartCMP(res.Host, "/web/check-updates.php")){
		osu_checkUpdates(res.Host, s);
	}else if (SafeStartCMP(res.Host, "/web/osu-osz2-getscores.php"))
		osu_getScores(res, s);
	/*else if (SafeStartCMP(res.Host, "/web/osu-search-set.php")){
		std::thread a(Handle_SearchSet,res, s);
		a.detach();
		DontCloseConnection = 1;
	}*/else if (SafeStartCMP(res.Host, "/web/osu-search.php")){
		//std::thread a(Thread_Handle_DirectSearch, std::string(res.Host.begin(), res.Host.end()), s);
		//a.detach();
		//DontCloseConnection = 1;
	}
	else if (SafeStartCMP(res.Host, "/web/osu-getreplay.php")){
		std::thread a(Thread_GetReplay, std::string(res.Host.begin(), res.Host.end()), s);
		a.detach();
		DontCloseConnection = 1;
	}else if (SafeStartCMP(res.Host, "/d/")) {
		//std::thread a(Thread_DownloadOSZ, std::string(res.Host.begin(), res.Host.end()), s);
		//a.detach();
		//DontCloseConnection = 1;
	}else if (SafeStartCMP(res.Host, "/web/maps/")){//used when updating a single maps .osu
		std::thread a(Thread_UpdateOSU, std::string(res.Host.begin()+1, res.Host.end()), s);
		a.detach();
		DontCloseConnection = 1;
	}
	else SendAria404(s);

	if (!DontCloseConnection){
		s.Dis();
		/*const unsigned long long Time = std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - begin).count();
		
		printf(KMAG"Aria> " KRESET "%fms\n", double(double(Time) / 1000000.0));*/
	}
}


void AriaWork(const DWORD ID){

	while (1){

		DWORD Count = 0;
		_Con* Req = 0;

		Count = AriaConnectionQue[ID].size();

		if (Count) {

			AriaThreadLock[ID].lock();

			Count = AriaConnectionQue[ID].size();

			Req = new _Con[Count];

			if (Req)
				memcpy(&Req[0], &AriaConnectionQue[ID][0], Count * sizeof(_Con));

			AriaConnectionQue[ID].clear();

			AriaThreadLock[ID].unlock();
		}

		for (DWORD i = 0; i < Count; i++)
			HandleAria(Req[i]);

		if (Req)delete[] Req;

		Sleep(1);//It being 0 literally hogs an entire core so.. No?
	}

}
bool ARIALOADED = 0;
void Aria_Main(){

	UpdateCache.reserve(16);//Really should only be a few anyway.

	const char * mName = "Aria";

	LogMessage("Starting", mName);

	{
		for (DWORD i = 0; i < ARIA_THREAD_COUNT; i++) {
			AriaConnectionQue[i].reserve(64);
			printf("	Aria%i: %i\n", i, int(AriaSQL[i].Connect()));
			std::thread a(AriaWork, i);
			a.detach();
		}
	}
	ARIALOADED = 1;


#ifndef LINUX
	SOCKET listening = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (listening == INVALID_SOCKET)
		return LogError("Failed to load socket", mName);

	sockaddr_in hint; 
	ZeroMemory(&hint.sin_addr, sizeof(hint.sin_addr));
	hint.sin_family = AF_INET;
	hint.sin_port = htons(ARIAPORT);
	
	::bind(listening, (sockaddr*)&hint, sizeof(hint));

	listen(listening, SOMAXCONN);// Sets the socket to listen

	sockaddr_in client;

	DWORD Time = 2500;
	DWORD MPL = MAX_PACKET_LENGTH;

	setsockopt(listening, SOL_SOCKET, SO_RCVTIMEO, (char*)&Time, 4);
	setsockopt(listening, SOL_SOCKET, SO_SNDTIMEO, (char*)&Time, 4);
	setsockopt(listening, SOL_SOCKET, SO_RCVBUF, (char*)&MPL, 4);
#else

	struct sockaddr_un serveraddr;

	SOCKET listening = socket(AF_UNIX, SOCK_STREAM, 0);

	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sun_family = AF_UNIX;
	strcpy(serveraddr.sun_path, ARIA_UNIX_SOCKET);

	unlink(ARIA_UNIX_SOCKET);

	if (bind(listening, (struct sockaddr *)&serveraddr, SUN_LEN(&serveraddr)) < 0) {
		perror("bind() failed");
		return;
	}
	if (listen(listening, SOMAXCONN) < 0) {
		perror("listen() failed");
		return;
	}
	chmod(ARIA_UNIX_SOCKET, S_IRWXU | S_IRWXG | S_IRWXO);

#endif
	DWORD ID = 0;

	while(1){
		
#ifndef LINUX
		int clientSize = sizeof(client);
		ZeroMemory(&client, clientSize);

		SOCKET s = accept(listening, (sockaddr*)&client, &clientSize);
#else
		SOCKET s = accept(listening, 0, 0);
#endif

		if (s == INVALID_SOCKET)continue;

		AriaThreadLock[ID].lock();

		AriaConnectionQue[ID].push_back(_Con(s,ID));

		AriaThreadLock[ID].unlock();

		ID++;
		if (ARIA_THREAD_COUNT >= ID)
			ID = 0;
	}
}