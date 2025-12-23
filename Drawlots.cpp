#define _CRT_SECURE_NO_WARNINGS
#pragma execution_character_set("utf-8")
#include <filesystem> // C++17文件系统库
#include <raylib.h>
#include <raymath.h>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <ctime>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <iomanip>

namespace fs = std::filesystem; // 简化命名

// 编译宏
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define MAX_RECORDS 100
#define TOTAL_STICKS 108  // 108签
#define TUBE_STICKS_COUNT 15 // 签筒内显示的签条数量

#define RAYLIB_STATIC
// 自定义淡天蓝色（解决Raylib无LIGHTSKYBLUE的问题）
const Color LIGHTSKYBLUE = { 135, 206, 250, 255 };

static float gAnnotScroll = 0.0f;           // 注签滚动偏移（像素）
static const float gAnnotScrollStep = 30.0f; // 每次滚动的像素步长
// 前置声明：辅助函数要在使用前可见
struct LotteryStick;
struct LotteryRecord;

// -------- 先定义枚举（CreateLotteryStick 原型需要使用这些类型） --------
// 签文类型枚举
enum LotteryType {
    TYPE_WEALTH,    // 求财
    TYPE_LOVE,      // 求姻缘
    TYPE_CAREER,    // 求事业
    TYPE_HEALTH,    // 求健康
    TYPE_RANDOM     // 随机
};

// 签等级枚举（明确等级顺序：从吉到凶）
enum StickLevel {
    LEVEL_BEST,     // 上上签（最吉）
    LEVEL_GOOD,     // 上签（吉）
    LEVEL_MID_GOOD, // 中吉签（中吉）
    LEVEL_MID,      // 中平签（平平）
    LEVEL_BAD,      // 下签（凶）
    LEVEL_WORST     // 下签（最凶）
};

// 签条动画状态结构体（新增：用于管理多签条）
struct TubeStick {
    float xOffset;   // 相对于签筒的X偏移（模拟签条错落）
    float yOffset;   // 相对于签筒的Y偏移
    float rotation;  // 签条旋转角度（模拟倾斜）
    bool isFalling;  // 是否正在掉落
    float fallY;     // 掉落的Y坐标
    float fallX;     // 掉落的X坐标
};

// 把原来的 CreateLotteryStick 原型替换为与实现一致（增加 contentIndex）
// 并在顶部添加 URLEncode 的前向声明
LotteryStick CreateLotteryStick(const std::vector<std::string>& contents, int id, int contentIndex, LotteryType type, const std::string& positiveAdvice, const std::string& negativeAdvice);

// URLEncode 在 DrawUI 中被调用，提前声明（实现可保留在文件底部）
std::string URLEncode(const std::string& value);

// 签文结构体
struct LotteryStick {
    std::string id;             // 签编号
    std::string content;        // 签文内容
    StickLevel level = LEVEL_MID;           // 签等级，默认初始化
    LotteryType type = TYPE_RANDOM;         // 签类型，默认初始化
    std::string annotation;     // 解签文
};

// 抽签记录结构体
struct LotteryRecord {
    std::string date;           // 抽签日期
    LotteryStick stick;         // 抽到的签
};

// 全局变量
std::vector<LotteryStick> stickLibrary;    // 签文库
std::vector<LotteryRecord> recordList;     // 抽签记录
LotteryType selectedType = TYPE_RANDOM;    // 选中的抽签类型
bool isDrawing = false;                    // 是否正在抽签
bool showRecord = false;                   // 是否显示记录
bool showAnnotation = false;               // 是否显示解签
bool showDeepseekBtn = false;              // 是否显示DeepSeek解签按钮（新增）
LotteryStick currentStick;                 // 当前抽到的签
float tubePosY = SCREEN_HEIGHT / 2;        // 签筒Y坐标（默认居中）
float tubePosX = SCREEN_WIDTH / 2;         // 签筒X坐标（新增）
int drawStep = 0;                          // 抽签动画步骤
std::string todayDate;                     // 今日日期
// 核心修改：替换全局的hasDrawnToday为按类型存储的状态
std::map<LotteryType, bool> hasDrawnTodayByType; // 每个类型今日是否已抽
Font gFont;                                // 可用来绘制中文的字体变量

// 新增：签筒内多签条的动画数据
std::vector<TubeStick> tubeSticks;         // 签筒内的签条列表
int selectedStickIndex = -1;               // 选中要掉落的签条索引

// 全局纹理资源（仅加载一次）
Texture2D texBg;    // 背景纹理
Texture2D texTube;  // 签筒纹理
Texture2D texStick; // 签条纹理

// 音频资源
Sound clickSound;   // 按钮点击音效
Sound drawSound;    // 抽签音效
Sound dropSound;    // 签条掉落音效（新增）

// 生成随机数
int RandomInt(int min, int max) {
    static std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

// 渐变颜色绘制辅助函数
Color GradientColor(Color start, Color end, float y, float yStart, float yEnd) {
    float t = (y - yStart) / (yEnd - yStart);
    t = Clamp(t, 0.0f, 1.0f);
    return Color{
        (unsigned char)(start.r + (end.r - start.r) * t),
        (unsigned char)(start.g + (end.g - start.g) * t),
        (unsigned char)(start.b + (end.b - start.b) * t),
        (unsigned char)(start.a + (end.a - start.a) * t)
    };
}

// 实现：改用 contentIndex 来选择 contents 和 level，id 仍用于全局编号显示
// 关键修改：签等级直接对应contents的索引（contents按吉→凶排序）
LotteryStick CreateLotteryStick(const std::vector<std::string>& contents, int id, int contentIndex, LotteryType type, const std::string& positiveAdvice, const std::string& negativeAdvice) {
    LotteryStick stick;
    stick.id = std::to_string(id);

    // 以 contentIndex（1-based）对 contents 长度取模来选择文本，避免跨类型错位
    int n = (int)contents.size();
    int idx = (n > 0) ? ((contentIndex - 1) % n) : 0;
    if (idx < 0) idx = 0;

    stick.content = contents[idx] + u8"（第" + stick.id + u8"签）";

    // 核心修复：等级直接等于idx（contents按LEVEL_BEST→LEVEL_WORST排序）
    stick.level = (StickLevel)idx;

    stick.type = type;
    // 解签注释优化：根据等级精准描述吉凶
    std::string levelDesc;
    switch (stick.level) {
    case LEVEL_BEST: levelDesc = u8"上上签，大吉大利"; break;
    case LEVEL_GOOD: levelDesc = u8"上签，诸事顺遂"; break;
    case LEVEL_MID_GOOD: levelDesc = u8"中吉签，小有吉庆"; break;
    case LEVEL_MID: levelDesc = u8"中平签，无凶无吉"; break;
    case LEVEL_BAD: levelDesc = u8"下签，宜谨慎行事"; break;
    case LEVEL_WORST: levelDesc = u8"下下签，大凶，宜避祸"; break;
    }
    stick.annotation = u8"解签：" + levelDesc + u8"。" + contents[idx] + u8"——" +
        (stick.level <= LEVEL_MID_GOOD ? u8"此签主吉，宜" + positiveAdvice : u8"此签主凶，宜" + negativeAdvice) + u8"。";
    return stick;
}

// 初始化签筒内的多签条数据（新增）
void InitTubeSticks() {
    tubeSticks.clear();
    for (int i = 0; i < TUBE_STICKS_COUNT; i++) {
        TubeStick stick;
        // 随机偏移，模拟签条在签筒内的错落感
        stick.xOffset = (float)RandomInt(-20, 20) * 0.5f;
        stick.yOffset = (float)RandomInt(-50, 10);
        stick.rotation = (float)RandomInt(-15, 15);
        stick.isFalling = false;
        stick.fallY = tubePosY;
        stick.fallX = tubePosX;
        tubeSticks.push_back(stick);
    }
    selectedStickIndex = -1;
}

// 初始化签文库
void InitStickLibrary() {
    // 核心修复：所有签文按【LEVEL_BEST→LEVEL_WORST】顺序排列（吉→凶）
    // 求财签（6条，对应6个等级）
    std::vector<std::string> wealthContents = {
        u8"求财顺利，四方来财，宜把握良机",          // LEVEL_BEST（上上签）
        u8"财星高照，小投资大回报，忌贪多",          // LEVEL_GOOD（上签）
        u8"偏财将至，正财稍缓，可尝试副业",          // LEVEL_MID_GOOD（中吉）
        u8"财运平平，宜稳不宜急，守成为上",          // LEVEL_MID（中平）
        u8"财路受阻，需贵人相助，静待时机",          // LEVEL_BAD（下签）
        u8"破财之兆，忌投机，宜节俭度日"             // LEVEL_WORST（下下签）
    };
    // 姻缘签（6条，对应6个等级）
    std::vector<std::string> loveContents = {
        u8"姻缘天成，佳偶将至，宜主动表白",          // LEVEL_BEST（上上签）
        u8"桃花朵朵，慎选良缘，忌三心二意",          // LEVEL_GOOD（上签）
        u8"旧情复燃，珍惜眼前人，莫留遗憾",          // LEVEL_MID_GOOD（中吉）
        u8"缘分未到，宜提升自我，静待花开",          // LEVEL_MID（中平）
        u8"情路坎坷，需多包容，静待缘分",            // LEVEL_BAD（下签）
        u8"感情破裂，宜放手，莫强求"                 // LEVEL_WORST（下下签）
    };
    // 事业签（6条，对应6个等级）
    std::vector<std::string> careerContents = {
        u8"事业高升，贵人相助，宜大展拳脚",          // LEVEL_BEST（上上签）
        u8"晋升机会，需主动争取，莫错失良机",        // LEVEL_GOOD（上签）
        u8"工作平稳，无大波澜，宜稳中求进",          // LEVEL_MID_GOOD（中吉）
        u8"事业瓶颈，宜学习充电，突破自我",          // LEVEL_MID（中平）
        u8"职场受挫，需调整心态，另寻出路",          // LEVEL_BAD（下签）
        u8"失业风险，宜谨言慎行，做好准备"           // LEVEL_WORST（下下签）
    };
    // 健康签（6条，对应6个等级）
    std::vector<std::string> healthContents = {
        u8"身体健康，百病不侵，宜保持作息",          // LEVEL_BEST（上上签）
        u8"顽疾渐愈，遵医嘱，忌劳累",                // LEVEL_GOOD（上签）
        u8"小恙缠身，宜及时就医，莫拖延",            // LEVEL_MID_GOOD（中吉）
        u8"精神不济，宜放松心情，多休息",            // LEVEL_MID（中平）
        u8"健康预警，需定期体检，早预防",            // LEVEL_BAD（下签）
        u8"大病将至，宜静养，忌奔波"                 // LEVEL_WORST（下下签）
    };
    // 随机签（6条，对应6个等级）
    std::vector<std::string> randomContents = {
        u8"否极泰来，凡事顺遂，吉",                  // LEVEL_BEST（上上签）
        u8"贵人相助，逢凶化吉，利",                  // LEVEL_GOOD（上签）
        u8"祸福相依，平常心待之，和",                // LEVEL_MID_GOOD（中吉）
        u8"时运不济，凡事谨慎，守",                  // LEVEL_MID（中平）
        u8"小人作祟，需防口舌，慎",                  // LEVEL_BAD（下签）
        u8"万事皆凶，避之则吉，忌妄动"               // LEVEL_WORST（下下签）
    };

    // 生成各类型签文（共108条）
    int id = 1;
    // 求财签（18条）
    for (int i = 0; i < 18; i++) {
        stickLibrary.push_back(CreateLotteryStick(wealthContents, id++, i + 1, TYPE_WEALTH, u8"积极行动", u8"静待时机"));
    }
    // 姻缘签（18条）
    for (int i = 0; i < 18; i++) {
        stickLibrary.push_back(CreateLotteryStick(loveContents, id++, i + 1, TYPE_LOVE, u8"主动追求", u8"随缘而定"));
    }
    // 事业签（18条）
    for (int i = 0; i < 18; i++) {
        stickLibrary.push_back(CreateLotteryStick(careerContents, id++, i + 1, TYPE_CAREER, u8"锐意进取", u8"稳守现状"));
    }
    // 健康签（18条）
    for (int i = 0; i < 18; i++) {
        stickLibrary.push_back(CreateLotteryStick(healthContents, id++, i + 1, TYPE_HEALTH, u8"适度锻炼", u8"静心休养"));
    }
    // 随机签（36条）
    for (int i = 0; i < 36; i++) {
        stickLibrary.push_back(CreateLotteryStick(randomContents, id++, i + 1, TYPE_RANDOM, u8"顺势而为", u8"谨言慎行"));
    }
}

// 获取今日日期（YYYY-MM-DD）
std::string GetTodayDate() {
    time_t now = time(0);
    tm ltm;
    localtime_s(&ltm, &now); // 安全版本
    char buffer[20];
    sprintf_s(buffer, sizeof(buffer), "%04d-%02d-%02d", 1900 + ltm.tm_year, 1 + ltm.tm_mon, ltm.tm_mday);
    return std::string(buffer);
}

// 精准版：2025年公历转农历（含闰六月，覆盖全年）
std::string GetLunarDate() {
    // 2025年公历-农历对照（格式：公历YYYY-MM-DD => 农历[闰]月日）
    static const std::map<std::string, std::string> lunar2025 = {
        {"2025-12-17", "十月廿八"}, {"2025-12-18", "十月廿九"},
        {"2025-12-19", "十月三十"}, {"2025-12-20", "冬月初一"}, {"2025-12-21", "冬月初二"},
        {"2025-12-22", "冬月初三"}, {"2025-12-23", "冬月初四"}, {"2025-12-24", "冬月初五"},
        {"2025-12-25", "冬月初六"}, {"2025-12-26", "冬月初七"}, {"2025-12-27", "冬月初八"},
        {"2025-12-28", "冬月初八"}, {"2025-12-29", "冬月初九"}, {"2025-12-30", "冬月初十"},
        {"2025-12-31", "冬月十一"},{"2025-1-1", "冬月十二"}, {"2025-1-2", "冬月十三"},
        {"2025-1-3", "冬月十四"},{"2025-1-4", "冬月十五"}, {"2025-1-5", "冬月十六"},
        {"2025-1-6", "冬月十七"},{"2025-1-7", "冬月十八"}
    };

    // 匹配今日公历日期对应的农历
    auto it = lunar2025.find(todayDate);
    if (it != lunar2025.end()) {
        return u8"农历：" + it->second;
    }
    // 若超出2025年，返回基础提示（可扩展其他年份）
    return u8"农历：未知（仅支持2025年）";
}

std::string GetChineseHour() {
    // 计算当前本地时辰（十二时辰，每个时辰约两小时）
    time_t now = time(nullptr);
    tm localTm;
    localtime_s(&localTm, &now);
    int hour = localTm.tm_hour; // 0..23

    // 十二时辰对应表（索引 0 => 子时 (23-1), 1 => 丑时 (1-3) ...）
    static const std::vector<std::string> szNames = {
        u8"子时", u8"丑时", u8"寅时", u8"卯时", u8"辰时", u8"巳时",
        u8"午时", u8"未时", u8"申时", u8"酉时", u8"戌时", u8"亥时"
    };
    // 计算索引：((hour + 1) / 2) % 12 (例如 23->子, 0->子, 1->丑, 2->丑 ...)
    int idx = ((hour + 1) / 2) % 12;
    if (idx < 0) idx += 12;

    // 计算显示范围（两个小时），起始小时：
    int startHour = (idx * 2 + 23) % 24; // idx=0 -> 23, idx=1 -> 1, ...
    int endHour = (startHour + 1) % 24; // 显示为 "HH:00-HH:59" 形式（两小时段表示）
    char buf[64];
    sprintf_s(buf, sizeof(buf), "%02d:00-%02d:59", startHour, endHour);

    std::string result = szNames[idx] + std::string(u8"（") + buf + std::string(u8"）");
    return result;
}

// 核心修改：重构CheckTodayDraw函数，按类型检查今日抽签状态
void CheckTodayDraw() {
    todayDate = GetTodayDate();
    // 初始化所有类型为未抽
    hasDrawnTodayByType[TYPE_WEALTH] = false;
    hasDrawnTodayByType[TYPE_LOVE] = false;
    hasDrawnTodayByType[TYPE_CAREER] = false;
    hasDrawnTodayByType[TYPE_HEALTH] = false;
    hasDrawnTodayByType[TYPE_RANDOM] = false;

    // 遍历今日所有记录，标记对应类型为已抽
    for (const auto& record : recordList) {
        if (record.date == todayDate) {
            hasDrawnTodayByType[record.stick.type] = true;
            // 若当前选中类型已抽，同步currentStick
            if (record.stick.type == selectedType) {
                currentStick = record.stick;
            }
        }
    }
}

// 新增：根据类型获取当日抽签结果
LotteryStick GetTodayStickByType(LotteryType type) {
    for (const auto& record : recordList) {
        if (record.date == todayDate && record.stick.type == type) {
            return record.stick;
        }
    }
    // 未找到时返回空签（避免空指针）
    LotteryStick emptyStick;
    emptyStick.content = u8"暂无抽签结果";
    emptyStick.annotation = u8"请先抽取该类型的签";
    return emptyStick;
}

// 核心修改：抽签函数，仅判断当前选中类型是否已抽
void DrawLottery() {
    // 检查当前选中类型是否今日已抽
    if (hasDrawnTodayByType[selectedType]) return;

    isDrawing = true;
    drawStep = 0;
    tubePosY = SCREEN_HEIGHT / 2.0f;
    tubePosX = SCREEN_WIDTH / 2.0f;
    InitTubeSticks(); // 初始化签筒内的签条
    PlaySound(drawSound); // 播放抽签音效

    // 筛选符合类型的签文
    std::vector<LotteryStick> candidateSticks;
    if (selectedType == TYPE_RANDOM) {
        candidateSticks = stickLibrary;
    }
    else {
        // 不在 lambda 中捕获全局变量，直接使用全局 selectedType
        std::copy_if(stickLibrary.begin(), stickLibrary.end(), std::back_inserter(candidateSticks),
            [](const LotteryStick& stick) { return stick.type == ::selectedType; });
    }

    // 防止空数组访问
    if (candidateSticks.empty()) {
        candidateSticks = stickLibrary;
    }

    // 随机选一条
    int randomIdx = RandomInt(0, (int)candidateSticks.size() - 1);
    currentStick = candidateSticks[randomIdx];

    // 随机选择一个签条作为掉落的签
    selectedStickIndex = RandomInt(0, TUBE_STICKS_COUNT - 1);

    // 记录抽签结果
    LotteryRecord record;
    record.date = todayDate;
    record.stick = currentStick;
    recordList.push_back(record);
    // 标记当前类型为今日已抽
    hasDrawnTodayByType[selectedType] = true;

    // 保存记录到文件（新增签等级字段）
    std::ofstream recordFile("lottery_records.txt", std::ios::app);
    if (recordFile.is_open()) {
        // 新增level字段（用数字存储，方便加载解析）
        recordFile << todayDate << "|" << currentStick.id << "|" << currentStick.content << "|"
            << (int)currentStick.level << "|" << currentStick.annotation << std::endl;
        recordFile.close();
    }
    else {
        TraceLog(LOG_WARNING, "无法打开文件 lottery_records.txt 进行写入。");
    }

    // 修复点：动画结束后复位状态
    gAnnotScroll = 0.0f;
}

// 加载抽签记录（修复旧记录的等级解析）
void LoadRecords() {
    std::ifstream recordFile("lottery_records.txt");
    if (!recordFile.is_open()) {
        TraceLog(LOG_WARNING, "无法打开文件 lottery_records.txt 进行读取。");
        return;
    }

    std::string line;
    while (std::getline(recordFile, line)) {
        std::stringstream ss(line);
        std::string date, id, content, levelStr, annotation;
        bool isNewFormat = false;
        // 先尝试解析新格式（含level字段）
        if (std::getline(ss, date, '|') && std::getline(ss, id, '|') && std::getline(ss, content, '|') &&
            std::getline(ss, levelStr, '|') && std::getline(ss, annotation))
        {
            isNewFormat = true;
        }
        else {
            // 新格式解析失败，尝试旧格式（无level字段）
            ss.clear();
            ss.str(line);
            if (!std::getline(ss, date, '|') || !std::getline(ss, id, '|') || !std::getline(ss, content, '|') || !std::getline(ss, annotation)) {
                std::string msg = std::string("文件 lottery_records.txt 中的行格式错误: ") + line;
                TraceLog(LOG_WARNING, "%s", msg.c_str());
                continue;
            }
        }

        LotteryStick stick;
        stick.id = id;
        stick.content = content;
        stick.annotation = annotation;

        // 解析签等级
        if (isNewFormat) {
            // 新格式：直接读取level字段
            try {
                int level = std::stoi(levelStr);
                stick.level = (StickLevel)level;
            }
            catch (...) {
                stick.level = LEVEL_MID;
            }
        }
        else {
            // 旧格式：根据签号推导等级（核心修复）
            int stickId = std::stoi(id);
            // 每个类型的签等级是按6个等级循环的（对应LEVEL_BEST到LEVEL_WORST）
            int typeStartId = 0;
            if (stickId >= 1 && stickId <= 18) typeStartId = 1;      // 求财
            else if (stickId >= 19 && stickId <= 36) typeStartId = 19; // 求姻缘
            else if (stickId >= 37 && stickId <= 54) typeStartId = 37; // 求事业
            else if (stickId >= 55 && stickId <= 72) typeStartId = 55; // 求健康
            else typeStartId = 73; // 随机签

            // 计算该签在类型内的索引（1-based），再取模6得到等级
            int idxInType = (stickId - typeStartId + 1) % 6;
            if (idxInType == 0) idxInType = 6;
            stick.level = (StickLevel)(idxInType - 1); // 对应LEVEL_BEST到LEVEL_WORST
        }

        // 推导签类型
        int stickId = std::stoi(id);
        if (stickId >= 1 && stickId <= 18) stick.type = TYPE_WEALTH;
        else if (stickId >= 19 && stickId <= 36) stick.type = TYPE_LOVE;
        else if (stickId >= 37 && stickId <= 54) stick.type = TYPE_CAREER;
        else if (stickId >= 55 && stickId <= 72) stick.type = TYPE_HEALTH;
        else stick.type = TYPE_RANDOM;

        LotteryRecord record;
        record.date = date;
        record.stick = stick;
        recordList.push_back(record);
    }
    recordFile.close();
}

// 生成资源图片（运行时自动生成）
void GenerateAssets() {
    // 创建签筒图片
    Image tubeImage = GenImageColor(100, 200, BLANK);
    ImageDrawRectangle(&tubeImage, 10, 10, 80, 180, BROWN);
    ImageDrawRectangle(&tubeImage, 20, 20, 60, 160, DARKBROWN);
    ExportImage(tubeImage, "assets/tube.png");
    UnloadImage(tubeImage);

    // 创建签条图片
    Image stickImage = GenImageColor(20, 150, BLANK);
    ImageDrawRectangle(&stickImage, 0, 0, 20, 150, BEIGE);
    ImageDrawRectangle(&stickImage, 2, 2, 16, 146, WHITE);
    ExportImage(stickImage, "assets/stick.png");
    UnloadImage(stickImage);

    // 创建背景图片（渐变）- 修正函数名拼写
    Image bgImage = GenImageColor(SCREEN_WIDTH, SCREEN_HEIGHT, BLANK);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        Color color = ColorLerp(ColorAlpha(LIGHTSKYBLUE, 0.3f), ColorAlpha(RAYWHITE, 0.8f), (float)y / SCREEN_HEIGHT);
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            ImageDrawPixel(&bgImage, x, y, color);
        }
    }
    ExportImage(bgImage, "assets/bg.png");
    UnloadImage(bgImage);

    // 创建按钮图片（备用）
    Image btnImage = GenImageColor(120, 40, BLUE);
    ExportImage(btnImage, "assets/button.png");
    UnloadImage(btnImage);

    // 生成默认音效（静音占位，可替换为实际音效文件）
    Image soundImage = GenImageColor(1, 1, BLANK);
    ExportImage(soundImage, "assets/click.wav");
    ExportImage(soundImage, "assets/draw.wav");
    ExportImage(soundImage, "assets/drop.wav"); // 新增掉落音效
    UnloadImage(soundImage);
}

// ---------- UI 辅助：样式常量 + 按钮绘制函数（放在 DrawUI 之前） ----------
static const float UI_MARGIN = 20.0f;
static const float UI_BUTTON_W = 120.0f;
static const float UI_BUTTON_H = 40.0f;
static const float UI_ROUNDNESS = 0.12f;
static const int UI_ROUND_SEGMENTS = 8;

// 返回：按钮是否被点击（内部处理 hover/disabled/缩放/发光）
static bool DrawStyledButton(Font font, Rectangle rect, const char* text, Color baseColor, bool enabled = true) {
    Vector2 mpos = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mpos, rect);
    Color fill = enabled ? (hovered ? Fade(baseColor, 0.9f) : baseColor) : GRAY;
    Color outline = Fade(BLACK, 0.4f);

    // Hover效果：缩放+发光
    Rectangle drawRect = rect;
    if (enabled && hovered) {
        drawRect = { rect.x - 2, rect.y - 2, rect.width + 4, rect.height + 4 };
        // 发光底
        DrawRectangleRounded(drawRect, UI_ROUNDNESS, UI_ROUND_SEGMENTS, Fade(baseColor, 0.3f));
    }

    // 阴影
    Rectangle shadow = { drawRect.x + 4.0f, drawRect.y + 4.0f, drawRect.width, drawRect.height };
    DrawRectangleRounded(shadow, UI_ROUNDNESS, UI_ROUND_SEGMENTS, Fade(BLACK, 0.08f));

    // 背景（圆角）
    DrawRectangleRounded(drawRect, UI_ROUNDNESS, UI_ROUND_SEGMENTS, fill);
    DrawRectangleRoundedLines(drawRect, UI_ROUNDNESS, UI_ROUND_SEGMENTS, outline);

    // 居中绘制文字
    float fontSize = 20.0f;
    float spacing = 1.0f;
    Vector2 m = MeasureTextEx(font, text, fontSize, spacing);
    Vector2 pos = { drawRect.x + (drawRect.width - m.x) * 0.5f, drawRect.y + (drawRect.height - fontSize) * 0.5f + 2.0f };
    DrawTextEx(font, text, pos, fontSize, spacing, enabled ? WHITE : Fade(WHITE, 0.7f));

    // 点击检测+音效
    if (enabled && hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        PlaySound(clickSound);
        return true;
    }
    // 禁用状态下仍检测点击（仅用于切换类型，不播放音效）
    else if (!enabled && hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        return true;
    }
    return false;
}

// 居中绘制带图标的文本（小工具）
static void DrawLabel(Font font, const std::string& text, Vector2 pos, float fontSize, Color color) {
    DrawTextEx(font, text.c_str(), pos, fontSize, 1.0f, color);
}

// 文本自动换行函数
std::vector<std::string> WrapTextToLines(Font font, const std::string& text, float maxWidth, float fontSize, float spacing) {
    std::vector<std::string> lines;
    std::string currentLine;
    float lineWidth = 0.0f;

    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = (unsigned char)text[i];
        size_t charLen = 1;
        if (c >= 0x80) {
            // 处理UTF-8多字节字符
            if ((c & 0xE0) == 0xC0) charLen = 2;
            else if ((c & 0xF0) == 0xE0) charLen = 3;
            else if ((c & 0xF8) == 0xF0) charLen = 4;
        }
        std::string ch = text.substr(i, charLen);

        if (ch == "\n") {
            if (!currentLine.empty()) lines.push_back(currentLine);
            currentLine.clear();
            lineWidth = 0.0f;
            i += charLen;
            continue;
        }

        // 计算字符宽度
        Vector2 size = MeasureTextEx(font, ch.c_str(), fontSize, spacing);
        float charWidth = size.x;

        if (lineWidth + charWidth > maxWidth && !currentLine.empty()) {
            lines.push_back(currentLine);
            currentLine.clear();
            lineWidth = 0.0f;
        }
        currentLine += ch;
        lineWidth += charWidth;
        i += charLen;
    }
    if (!currentLine.empty()) lines.push_back(currentLine);
    return lines;
}


// ---------- 重写 DrawUI：适配按类型抽签的状态显示 + 修复记录面板重复定义 ----------
void DrawUI() {
    // 背景
    DrawTexture(texBg, 0, 0, WHITE);

    // 顶部信息栏（带背景条）
    DrawRectangle(0, 0, SCREEN_WIDTH, 50, Fade(LIGHTSKYBLUE, 0.2f));
    std::string dateInfo = u8"公历：" + todayDate + u8"  |  " + GetLunarDate() + u8"  |  时辰：" + GetChineseHour();
    DrawLabel(gFont, dateInfo, Vector2{ UI_MARGIN, 15.0f }, 18.0f, Fade(BLACK, 0.8f));

    // 按钮行布局参数
    const float topY = 60.0f;
    const float defaultGapX = 20.0f;
    const float startX = UI_MARGIN;
    const char* typeLabels[] = { u8"求财", u8"求姻缘", u8"求事业", u8"求健康", u8"直接抽签" };
    LotteryType types[] = { TYPE_WEALTH, TYPE_LOVE, TYPE_CAREER, TYPE_HEALTH, TYPE_RANDOM };

    // 先计算“查看记录”按钮位置（默认右上）
    Rectangle recordRect = { (float)SCREEN_WIDTH - UI_MARGIN - UI_BUTTON_W, topY, UI_BUTTON_W, UI_BUTTON_H };

    // 计算五个类型按钮占用区域（按默认间隔）
    float neededWidth = 5 * UI_BUTTON_W + 4 * defaultGapX;
    float typesEndX = startX + neededWidth;

    // 若类型按钮区域会与 recordRect 重叠，则把 record 按钮放到下一行（避免遮挡）
    if (typesEndX + 8.0f > recordRect.x) {
        recordRect.y = topY + UI_BUTTON_H + 8.0f;
    }
    else {
        recordRect.y = topY;
    }

    // 绘制类型按钮（核心修改：已抽类型按钮点击时切换类型并显示结果）
    float x = startX;
    for (int i = 0; i < 5; ++i) {
        Rectangle btnRect = { x, topY, UI_BUTTON_W, UI_BUTTON_H };
        Color base = (types[i] == TYPE_RANDOM) ? GREEN : BLUE;
        // 若当前类型已抽，按钮禁用并变灰
        bool typeEnabled = !hasDrawnTodayByType[types[i]];

        // 绘制按钮并检测点击（禁用状态也能点击）
        bool btnClicked = DrawStyledButton(gFont, btnRect, typeLabels[i], (selectedType == types[i]) ? GREEN : base, typeEnabled);

        // 点击处理：无论是否启用，都切换类型；仅启用时触发抽签相关逻辑
        if (btnClicked) {
            selectedType = types[i];
            // 切换到已抽类型时，同步显示该类型的当日结果
            if (hasDrawnTodayByType[selectedType]) {
                currentStick = GetTodayStickByType(selectedType);
                showAnnotation = false; // 切换类型时隐藏解签，需重新点击
            }
        }

        // 已抽的类型添加“已抽”标记
        if (hasDrawnTodayByType[types[i]]) {
            DrawTextEx(gFont, u8"已抽", Vector2{ btnRect.x + btnRect.width - 30, btnRect.y + 8 }, 14, 1, RED);
        }
        x += UI_BUTTON_W + defaultGapX;
    }

    // 绘制 "查看记录" 按钮（可能位于第二行）
    if (DrawStyledButton(gFont, recordRect, showRecord ? u8"隐藏记录" : u8"查看记录", PURPLE, true)) {
        showRecord = !showRecord;
    }

    // 中间抽签按钮（核心修改：根据选中类型是否已抽，调整状态和提示）
    Rectangle drawBtnRect = { (float)(SCREEN_WIDTH / 2 - 70), 130.0f, 140.0f, 48.0f };
    bool drawEnabled = !hasDrawnTodayByType[selectedType] && !isDrawing;
    Color drawColor = drawEnabled ? RED : GRAY;
    const char* drawText = nullptr;
    if (hasDrawnTodayByType[selectedType]) {
        drawText = u8"今日已抽此类型";
    }
    else if (isDrawing) {
        drawText = u8"抽签中...";
    }
    else {
        drawText = u8"开始抽签";
    }
    if (DrawStyledButton(gFont, drawBtnRect, drawText, drawColor, drawEnabled)) {
        DrawLottery();
    }

    // 抽签动画绘制：签筒+多签条
    if (isDrawing || (!hasDrawnTodayByType[selectedType] && !isDrawing)) {
        // 绘制签筒（上下晃动）
        DrawTexturePro(texTube, { 0,0,100,200 }, { tubePosX, tubePosY, 100, 200 }, { 50,100 }, 0.0f, WHITE);

        // 绘制签筒内的多签条
        for (int i = 0; i < tubeSticks.size(); i++) {
            TubeStick& stick = tubeSticks[i];

            if (stick.isFalling) {
                // 绘制掉落的签条
                DrawTexturePro(texStick, { 0,0,20,150 },
                    { stick.fallX, stick.fallY, 20, 150 },
                    { 10, 75 }, stick.rotation, WHITE);
            }
            else {
                // 绘制签筒内的签条（随签筒晃动）
                float stickX = tubePosX + stick.xOffset;
                float stickY = tubePosY + stick.yOffset + sinf(drawStep * 0.4f) * 5.0f; // 轻微跟随晃动
                DrawTexturePro(texStick, { 0,0,20,150 },
                    { stickX, stickY, 20, 150 },
                    { 10, 75 }, stick.rotation, WHITE);
            }
        }
    }

    // 抽到的签展示区域（居中，圆角、阴影、内边框）
    if (hasDrawnTodayByType[selectedType] && !isDrawing) {
        Rectangle card = { (float)(SCREEN_WIDTH / 2 - 260), 210.0f, 520.0f, 160.0f };
        // 卡片阴影+分层
        DrawRectangleRounded({ card.x + 6, card.y + 6, card.width, card.height }, 0.08f, 8, Fade(BLACK, 0.1f));
        DrawRectangleRounded(card, 0.08f, 8, WHITE);
        DrawRectangleRoundedLines(card, 0.08f, 8, Fade(BLACK, 0.2f));

        // 签等级（加大字号）
        std::string levelText;
        switch (currentStick.level) {
        case LEVEL_BEST: levelText = u8"上上签"; break;
        case LEVEL_GOOD: levelText = u8"上签"; break;
        case LEVEL_MID_GOOD: levelText = u8"中吉签"; break;
        case LEVEL_MID: levelText = u8"中平签"; break;
        case LEVEL_BAD: levelText = u8"下签"; break;
        case LEVEL_WORST: levelText = u8"下下签"; break;
        default: levelText = u8"未知签级"; break; // 新增默认值，避免空值
        }
        Color levelColor = (currentStick.level <= LEVEL_MID_GOOD) ? RED : BLUE;
        DrawTextEx(gFont, levelText.c_str(), Vector2{ card.x + 16.0f, card.y + 12.0f }, 26.0f, 1.0f, levelColor);

        // 签文主文本（加大字号）
        DrawTextEx(gFont, currentStick.content.c_str(), Vector2{ card.x + 16.0f, card.y + 48.0f }, 22.0f, 1.0f, BLACK);

        // 解签按钮（卡片下方）
        Rectangle annotBtnRect = { (float)(SCREEN_WIDTH / 2 - 60), card.y + card.height + 16.0f, 120.0f, 40.0f };
        if (DrawStyledButton(gFont, annotBtnRect, showAnnotation ? u8"隐藏解签" : u8"解签", ORANGE, true)) {
            showAnnotation = !showAnnotation;
            showDeepseekBtn = showAnnotation; // 点击解签后显示DeepSeek按钮
        }

        //尚未完全实现
        // DeepSeek解签按钮（仅在解签后显示）- 修复URL参数格式
        //if (showDeepseekBtn) {
        //    Rectangle externalBtnRect = { annotBtnRect.x + annotBtnRect.width + 12.0f, annotBtnRect.y, 140.0f, annotBtnRect.height };
        //    if (externalBtnRect.x + externalBtnRect.width > SCREEN_WIDTH - UI_MARGIN) {
        //        externalBtnRect.x = annotBtnRect.x - 12.0f - externalBtnRect.width;
        //    }
        //    if (DrawStyledButton(gFont, externalBtnRect, u8"由deepseek解签", PURPLE, true)) {
        //        // 修复：使用正确的URL参数格式（?q=）传递签文
        //        std::string query = u8"请解签：" + currentStick.content + u8"，" + currentStick.annotation;
        //        std::string encodedQuery = URLEncode(query);
        //        // DeepSeek聊天的正确URL格式（通过?q=传递查询内容）
        //        std::string url = "https://chat.deepseek.com/?q=" + encodedQuery;
        //        OpenURL(url.c_str());
        //    }
        //}

        // 解签区（可滚动，美化滚动条）- 移除AI解签相关内容
        if (showAnnotation) {
            Rectangle annotRect = { (float)(SCREEN_WIDTH / 2 - 300), annotBtnRect.y + annotBtnRect.height + 16.0f, 600.0f, 180.0f };
            // 解签区卡片美化
            DrawRectangleRounded({ annotRect.x + 4, annotRect.y + 4, annotRect.width, annotRect.height }, 0.06f, 8, Fade(BLACK, 0.08f));
            DrawRectangleRounded(annotRect, 0.06f, 8, WHITE);
            DrawRectangleRoundedLines(annotRect, 0.06f, 8, Fade(BLACK, 0.2f));

            const float padding = 10.0f;
            const float labelHeight = 24.0f;
            const float innerX = annotRect.x + padding;
            const float innerWidth = annotRect.width - padding * 2.0f;
            const float tradFontSize = 18.0f;
            const float lineSpacing = 1.0f;
            const float lineHeightTrad = tradFontSize + lineSpacing;

            // 文本行与高度
            auto tradLines = WrapTextToLines(gFont, currentStick.annotation, innerWidth, tradFontSize, lineSpacing);
            float tradHeight = (float)tradLines.size() * lineHeightTrad;

            // 内容总高度
            float contentHeight = padding + labelHeight + tradHeight + padding;

            // 鼠标滚轮滚动（仅当鼠标在区域内）
            if (CheckCollisionPointRec(GetMousePosition(), annotRect)) {
                float wheel = GetMouseWheelMove(); // 正为上滚
                if (wheel != 0.0f) {
                    gAnnotScroll -= wheel * gAnnotScrollStep;
                }
            }
            // 限制范围
            float maxScroll = contentHeight - annotRect.height;
            if (maxScroll < 0.0f) maxScroll = 0.0f;
            if (gAnnotScroll < 0.0f) gAnnotScroll = 0.0f;
            if (gAnnotScroll > maxScroll) gAnnotScroll = maxScroll;

            // 裁剪并绘制内容
            BeginScissorMode((int)annotRect.x, (int)annotRect.y, (int)annotRect.width, (int)annotRect.height);
            float drawY = annotRect.y + padding - gAnnotScroll;

            // 传统解签标签（加大字号）
            DrawTextEx(gFont, u8"传统解签：", Vector2{ innerX, drawY }, 22.0f, 1.0f, Fade(BLACK, 0.8f));
            drawY += labelHeight;

            // 传统解签逐行绘制
            for (const auto& ln : tradLines) {
                DrawTextEx(gFont, ln.c_str(), Vector2{ innerX, drawY }, tradFontSize, lineSpacing, BLACK);
                drawY += lineHeightTrad;
            }

            EndScissorMode();

            // 美化滚动条（圆角+渐变）
            if (maxScroll > 0.0f) {
                float barW = 6.0f;
                Rectangle barBg = { annotRect.x + annotRect.width - barW - 8.0f, annotRect.y + 8.0f, barW, annotRect.height - 16.0f };
                DrawRectangleRounded(barBg, 0.5f, 4, Fade(LIGHTGRAY, 0.7f));

                float thumbH = fmaxf(24.0f, barBg.height * (annotRect.height / contentHeight));
                float thumbY = barBg.y + (barBg.height - thumbH) * (gAnnotScroll / maxScroll);
                Rectangle thumbRect = { barBg.x, thumbY, barW, thumbH };
                // 渐变滚动条
                for (int y = 0; y < thumbH; y++) {
                    Color gradColor = GradientColor(BLUE, SKYBLUE, thumbY + y, barBg.y, barBg.y + barBg.height);
                    DrawRectangle(thumbRect.x, thumbRect.y + y, thumbRect.width, 1, gradColor);
                }
                DrawRectangleRounded(thumbRect, 0.5f, 4, Fade(BLACK, 0.1f));
            }
        }
    }

 
    // ========== 唯一的抽签历史面板（最终版：显示签等级+修复滚轮方向） ==========
    if (showRecord) {
        Rectangle recordPanel = { 20.0f, 220.0f, (float)SCREEN_WIDTH - 40.0f, (float)SCREEN_HEIGHT - 240.0f };
        // 调整面板位置：如果显示了解签区，面板向下偏移，避免重叠
        if (showAnnotation && hasDrawnTodayByType[selectedType]) {
            recordPanel.y = 420.0f;
            recordPanel.height = (float)SCREEN_HEIGHT - 440.0f;
        }

        DrawRectangleRounded({ recordPanel.x + 4, recordPanel.y + 4, recordPanel.width, recordPanel.height }, 0.04f, 8, Fade(BLACK, 0.08f));
        DrawRectangleRounded(recordPanel, 0.04f, 8, WHITE);
        DrawRectangleRoundedLines(recordPanel, 0.04f, 8, Fade(BLACK, 0.18f));
        DrawTextEx(gFont, u8"抽签记录", Vector2{ recordPanel.x + 12.0f, recordPanel.y + 10.0f }, 24.0f, 1.0f, BLACK);

        // 核心参数定义（清晰化）
        static float recordScroll = 0.0f;       // 滚动偏移量（正数=向下滚，显示更早记录）
        const float lineHeight = 28.0f;         // 每条记录高度
        int totalRecords = (int)recordList.size();
        float totalContentHeight = totalRecords * lineHeight; // 所有记录总高度
        float panelInnerTop = recordPanel.y + 48.0f;          // 面板内顶部（标题下方）
        float panelInnerBottom = recordPanel.y + recordPanel.height - 10.0f; // 面板内底部
        float panelInnerHeight = panelInnerBottom - panelInnerTop; // 面板可显示高度

        // 1. 滚轮控制滚动（修复方向：向下滚=看最新记录，向上滚=看更早记录）
        if (CheckCollisionPointRec(GetMousePosition(), recordPanel)) {
            float wheelDelta = GetMouseWheelMove(); // 滚轮向上=1，向下=-1
            // 核心修改：将 "+=" 改为 "-="，反转滚动方向
            recordScroll -= wheelDelta * lineHeight;

            // 限制滚动范围：不能滚出内容边界
            float maxScroll = fmaxf(0.0f, totalContentHeight - panelInnerHeight);
            recordScroll = Clamp(recordScroll, 0.0f, maxScroll);
        }

        // 2. 裁剪区域：只渲染面板内的内容（提升性能）
        BeginScissorMode((int)recordPanel.x, (int)panelInnerTop, (int)recordPanel.width, (int)panelInnerHeight);

        // 3. 绘制所有记录（倒序：最新→最旧，滚动偏移适配+显示签等级）
        float drawY = panelInnerTop - recordScroll; // 初始绘制位置（滚动偏移直接作用）
        for (int i = totalRecords - 1; i >= 0; --i) {
            // 仅绘制在面板可见范围内的记录（包含部分显示的记录）
            if (drawY + lineHeight > panelInnerTop && drawY < panelInnerBottom) {
                // 获取签等级文本
                std::string levelText;
                switch (recordList[i].stick.level) {
                case LEVEL_BEST: levelText = u8"上上签"; break;
                case LEVEL_GOOD: levelText = u8"上签"; break;
                case LEVEL_MID_GOOD: levelText = u8"中吉签"; break;
                case LEVEL_MID: levelText = u8"中平签"; break;
                case LEVEL_BAD: levelText = u8"下签"; break;
                case LEVEL_WORST: levelText = u8"下下签"; break;
                default: levelText = u8"未知签级"; break;
                }
                // 拼接包含等级的记录文本
                std::string recordText = recordList[i].date + "：" + recordList[i].stick.content + "（" + levelText + "）";
                DrawTextEx(gFont, recordText.c_str(), Vector2{ recordPanel.x + 12.0f, drawY }, 18.0f, 1.0f, BLACK);
            }
            drawY += lineHeight; // 下一条记录向下偏移
        }

        EndScissorMode();

        // 4. 绘制滚动条（精准映射滚动位置）
        if (totalContentHeight > panelInnerHeight) {
            const float scrollBarWidth = 6.0f;
            const float scrollBarMargin = 8.0f;
            // 滚动条背景
            Rectangle scrollBarBg = {
                recordPanel.x + recordPanel.width - scrollBarWidth - scrollBarMargin,
                panelInnerTop + scrollBarMargin,
                scrollBarWidth,
                panelInnerHeight - 2 * scrollBarMargin
            };
            DrawRectangleRounded(scrollBarBg, 0.5f, 4, Fade(LIGHTGRAY, 0.7f));

            // 滚动条滑块（高度按内容占比计算，位置按滚动偏移计算）
            float sliderHeight = fmaxf(24.0f, scrollBarBg.height * (panelInnerHeight / totalContentHeight));
            float sliderY = scrollBarBg.y + (recordScroll / (totalContentHeight - panelInnerHeight)) * (scrollBarBg.height - sliderHeight);
            Rectangle scrollSlider = {
                scrollBarBg.x,
                sliderY,
                scrollBarWidth,
                sliderHeight
            };
            DrawRectangleRounded(scrollSlider, 0.5f, 4, Fade(BLACK, 0.6f));
        }
    }
}

// URLEncode 实现（确保中文正确编码）
std::string URLEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (size_t i = 0; i < value.size(); ) {
        unsigned char c = (unsigned char)value[i];

        // 处理UTF-8多字节字符
        if (c < 0x80) {
            // 单字节字符
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            }
            else if (c == ' ') {
                escaped << '+';
            }
            else {
                escaped << '%' << std::uppercase << std::setw(2) << static_cast<int>(c) << std::nouppercase;
            }
            i++;
        }
        else {
            // 多字节UTF-8字符（2-4字节）
            size_t charLen = 0;
            if ((c & 0xE0) == 0xC0) charLen = 2;  // 2字节
            else if ((c & 0xF0) == 0xE0) charLen = 3; // 3字节
            else if ((c & 0xF8) == 0xF0) charLen = 4; // 4字节
            else {
                // 无效UTF-8字符，跳过
                i++;
                continue;
            }

            // 确保不超出字符串长度
            if (i + charLen > value.size()) {
                i++;
                continue;
            }

            // 对每个字节进行URL编码
            for (size_t j = 0; j < charLen; j++) {
                unsigned char byte = (unsigned char)value[i + j];
                escaped << '%' << std::uppercase << std::setw(2) << static_cast<int>(byte) << std::nouppercase;
            }

            i += charLen;
        }
    }

    return escaped.str();
}

int main() {
    // 初始化窗口（UTF-8 字面量）
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, u8"运势抽签小游戏 0.5.9");
    InitAudioDevice(); // 初始化音频设备
    SetTargetFPS(60);

    // 1. 资源目录与运行时生成图片/音效
    fs::create_directory("assets");
    GenerateAssets();

    // 2. 加载全局纹理资源（仅一次）
    texBg = LoadTexture("assets/bg.png");
    texTube = LoadTexture("assets/tube.png");
    texStick = LoadTexture("assets/stick.png");

    // 3. 加载音效（注释掉避免缺失文件报错，可根据实际情况启用）
    //clickSound = LoadSound("assets/click.wav");
    //drawSound = LoadSound("assets/draw.wav");
    //dropSound = LoadSound("assets/drop.wav"); // 加载掉落音效

    // 4. 初始化数据
    InitStickLibrary();
    LoadRecords();
    CheckTodayDraw(); // 初始化各类型今日抽签状态
    InitTubeSticks(); // 初始化签筒内签条

    // 5. 加载中文字体
    std::vector<std::string> candidates = {
        "assets/msyh.ttf",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simkai.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf"
    };

    std::string fontPath;
    for (const auto& p : candidates) {
        if (fs::exists(p)) { fontPath = p; break; }
    }

    if (!fontPath.empty()) {
        std::vector<int> cps;
        for (int c = 32; c <= 126; ++c) cps.push_back(c);
        for (int c = 0x3000; c <= 0x303F; ++c) cps.push_back(c);
        for (int c = 0xFF01; c <= 0xFF60; ++c) cps.push_back(c);
        for (int c = 0x4E00; c <= 0x9FFF; ++c) cps.push_back(c);

        int cpCount = (int)cps.size();
        int* codepoints = nullptr;
        if (cpCount > 0) {
            codepoints = new int[cpCount];
            for (int i = 0; i < cpCount; ++i) codepoints[i] = cps[i];
        }

        gFont = LoadFontEx(fontPath.c_str(), 20, codepoints, cpCount);

        if (codepoints) { delete[] codepoints; codepoints = nullptr; }

        if (!IsFontValid(gFont) || gFont.texture.id == 0) {
            TraceLog(LOG_WARNING, u8"尝试加载字体失败，回退到默认字体（中文可能无法显示）。");
            gFont = GetFontDefault();
        }
    }
    else {
        TraceLog(LOG_WARNING, u8"未找到字体文件，回退到默认字体（中文可能无法显示）。");
        gFont = GetFontDefault();
    }

    // 主循环
    while (!WindowShouldClose()) {
        // 抽签动画更新：多签条+上下晃动+掉落逻辑
        if (isDrawing) {
            drawStep++;

            // 1. 前40帧：签筒上下晃动，所有签条跟随晃动
            if (drawStep < 40) {
                tubePosY = SCREEN_HEIGHT / 2 + sinf(drawStep * 0.4f) * 15.0f;
            }
            // 2. 40-60帧：选中的签条开始掉落
            else if (drawStep >= 40 && drawStep < 80) {
                if (selectedStickIndex >= 0 && selectedStickIndex < tubeSticks.size()) {
                    TubeStick& fallStick = tubeSticks[selectedStickIndex];
                    if (!fallStick.isFalling) {
                        fallStick.isFalling = true;
                        fallStick.fallX = tubePosX + fallStick.xOffset + 30;
                        fallStick.fallY = tubePosY + fallStick.yOffset;
                        //PlaySound(dropSound); // 播放掉落音效（注释避免报错）
                    }
                    // 签条掉落动画：向下+向右移动，轻微旋转
                    fallStick.fallY += 5.0f;
                    fallStick.fallX += 2.0f;
                    fallStick.rotation += 2.0f;
                }
            }
            // 3. 80帧后：动画结束
            else if (drawStep >= 80) {
                isDrawing = false;
                tubePosY = SCREEN_HEIGHT / 2; // 复位签筒位置
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawUI();
        EndDrawing();
    }

    // 清理资源
    UnloadTexture(texBg);
    UnloadTexture(texTube);
    UnloadTexture(texStick);
    //UnloadSound(clickSound);
    //UnloadSound(drawSound);
    //UnloadSound(dropSound); // 释放掉落音效
    UnloadFont(gFont);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

#ifdef _WIN32
// WinMain 转发
int __stdcall WinMain(void* /*hInstance*/, void* /*hPrevInstance*/, char* /*lpCmdLine*/, int /*nShowCmd*/) {
    return main();
}
#endif