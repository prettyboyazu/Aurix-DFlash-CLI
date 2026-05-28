/*
 * AURIX TC3XX UCB 使用说明与配置指南 - 文档生成脚本
 * 使用 docx npm 包生成 .docx 文件
 */

const path = require('path');
const fs = require('fs');

// 加载全局安装的 docx 包
const globalNodeModules = 'C:\\Users\\Yanhui.Wu\\AppData\\Roaming\\npm\\node_modules';
if (!process.env.NODE_PATH || !process.env.NODE_PATH.includes(globalNodeModules)) {
    process.env.NODE_PATH = globalNodeModules;
    require('module').Module._initPaths();
}

const {
    Document, Packer, Paragraph, TextRun, HeadingLevel, AlignmentType,
    Table, TableRow, TableCell, WidthType, BorderStyle, ShadingType,
    PageNumber, Header, Footer, PageOrientation, LevelFormat,
    convertInchesToTwip, convertMillimetersToTwip
} = require('docx');

// ================== 样式辅助函数 ==================
const FONT_HEADING = '微软雅黑';
const FONT_BODY = '宋体';
const FONT_CODE = 'Consolas';

function H1(text) {
    return new Paragraph({
        spacing: { before: 360, after: 200 },
        children: [
            new TextRun({ text, bold: true, size: 36, font: FONT_HEADING, color: '1F3864' })
        ],
    });
}

function H2(text) {
    return new Paragraph({
        spacing: { before: 280, after: 160 },
        children: [
            new TextRun({ text, bold: true, size: 28, font: FONT_HEADING, color: '2E74B5' })
        ],
    });
}

function H3(text) {
    return new Paragraph({
        spacing: { before: 200, after: 120 },
        children: [
            new TextRun({ text, bold: true, size: 24, font: FONT_HEADING, color: '2E74B5' })
        ],
    });
}

function P(text, opts = {}) {
    return new Paragraph({
        spacing: { line: 320, after: 100 },
        alignment: AlignmentType.JUSTIFIED,
        children: [
            new TextRun({ text, size: 22, font: FONT_BODY, ...opts })
        ],
    });
}

function Bullet(text, level = 0) {
    return new Paragraph({
        bullet: { level },
        spacing: { line: 300, after: 80 },
        children: [
            new TextRun({ text, size: 22, font: FONT_BODY })
        ],
    });
}

// 内联富文本段落，支持代码片段标注
function RichP(parts) {
    return new Paragraph({
        spacing: { line: 320, after: 100 },
        children: parts.map(p => new TextRun({
            text: p.text,
            size: 22,
            font: p.code ? FONT_CODE : FONT_BODY,
            bold: !!p.bold,
            color: p.code ? 'C7254E' : (p.color || undefined),
        })),
    });
}

function CodeBlock(lines) {
    return lines.map(line => new Paragraph({
        spacing: { line: 260, after: 0, before: 0 },
        shading: { type: ShadingType.CLEAR, fill: 'F4F4F4' },
        children: [
            new TextRun({ text: line || ' ', size: 20, font: FONT_CODE, color: '333333' })
        ],
    }));
}

function Note(text) {
    return new Paragraph({
        spacing: { before: 120, after: 120, line: 300 },
        shading: { type: ShadingType.CLEAR, fill: 'FFF4CE' },
        border: {
            left: { style: BorderStyle.SINGLE, size: 24, color: 'E6A700' },
        },
        children: [
            new TextRun({ text: '⚠ ' + text, size: 22, font: FONT_BODY, color: '7A4F00' })
        ],
    });
}

// ================== 表格构建 ==================
const TABLE_BORDER = {
    top:    { style: BorderStyle.SINGLE, size: 4, color: '8FAADC' },
    bottom: { style: BorderStyle.SINGLE, size: 4, color: '8FAADC' },
    left:   { style: BorderStyle.SINGLE, size: 4, color: '8FAADC' },
    right:  { style: BorderStyle.SINGLE, size: 4, color: '8FAADC' },
    insideHorizontal: { style: BorderStyle.SINGLE, size: 4, color: '8FAADC' },
    insideVertical:   { style: BorderStyle.SINGLE, size: 4, color: '8FAADC' },
};

function headerCell(text, width) {
    return new TableCell({
        width: { size: width, type: WidthType.PERCENTAGE },
        shading: { type: ShadingType.CLEAR, fill: '1F3864' },
        children: [new Paragraph({
            alignment: AlignmentType.CENTER,
            children: [new TextRun({ text, bold: true, size: 22, font: FONT_HEADING, color: 'FFFFFF' })]
        })],
    });
}

function bodyCell(text, width, opts = {}) {
    return new TableCell({
        width: { size: width, type: WidthType.PERCENTAGE },
        shading: opts.shade ? { type: ShadingType.CLEAR, fill: 'F2F6FC' } : undefined,
        children: [new Paragraph({
            alignment: opts.align || AlignmentType.LEFT,
            children: [new TextRun({
                text,
                size: 20,
                font: opts.code ? FONT_CODE : FONT_BODY,
                bold: !!opts.bold,
            })]
        })],
    });
}

function buildTable(headers, rows, widths) {
    const headerRow = new TableRow({
        tableHeader: true,
        children: headers.map((h, i) => headerCell(h, widths[i])),
    });
    const dataRows = rows.map((r, ri) => new TableRow({
        children: r.map((c, ci) => bodyCell(c, widths[ci], {
            shade: ri % 2 === 1,
            code: ci === 1 || ci === 0,
            align: (ci === 0 || ci === 1) ? AlignmentType.CENTER : AlignmentType.LEFT,
        })),
    }));
    return new Table({
        width: { size: 100, type: WidthType.PERCENTAGE },
        borders: TABLE_BORDER,
        rows: [headerRow, ...dataRows],
    });
}

// ================== 文档主体 ==================
const children = [];

// 封面标题
children.push(new Paragraph({
    spacing: { before: 1200, after: 400 },
    alignment: AlignmentType.CENTER,
    children: [new TextRun({ text: 'AURIX TC3XX UCB', bold: true, size: 56, font: FONT_HEADING, color: '1F3864' })],
}));
children.push(new Paragraph({
    spacing: { after: 1000 },
    alignment: AlignmentType.CENTER,
    children: [new TextRun({ text: '使用说明与配置指南', bold: true, size: 44, font: FONT_HEADING, color: '2E74B5' })],
}));
children.push(new Paragraph({
    spacing: { after: 200 },
    alignment: AlignmentType.CENTER,
    children: [new TextRun({ text: 'User Configuration Block — Reference & Operation Manual', italics: true, size: 26, font: FONT_HEADING, color: '595959' })],
}));
children.push(new Paragraph({
    spacing: { before: 2400 },
    alignment: AlignmentType.CENTER,
    children: [new TextRun({ text: '基于 Infineon AURIX TC3xx 系列', size: 22, font: FONT_BODY, color: '595959' })],
}));
children.push(new Paragraph({
    alignment: AlignmentType.CENTER,
    children: [new TextRun({ text: '配套工具：dflash.exe', size: 22, font: FONT_BODY, color: '595959' })],
}));
children.push(new Paragraph({ pageBreakBefore: true, children: [] }));

// =============== 1. 概述 ===============
children.push(H1('1. 概述'));
children.push(P('UCB（User Configuration Block，用户配置块）是 AURIX TC3xx 系列单片机内置的一组关键配置存储区，位于 DFlash 的特殊保留区段。它在芯片上电及复位过程中由启动 ROM 读取，决定了启动行为、Flash 保护、调试接口访问、HSM 安全功能等芯片层级的核心策略。'));

children.push(H2('1.1 基本属性'));
children.push(Bullet('定义：User Configuration Block，用户配置块'));
children.push(RichP([
    { text: '存储位置：DFlash 的 UCB 区域，地址范围 ' },
    { text: '0xAF400000 – 0xAF405FFF', code: true },
]));
children.push(Bullet('总容量：24 KB'));
children.push(Bullet('Sector 数量：48 个，每个 sector 512 字节'));
children.push(Bullet('擦除/编程粒度：以 sector（512B）为单位'));
children.push(P('UCB 内容覆盖：启动模式头（BMHD）、OTP、DFlash/PFlash 保护密码、调试接口（DAP）保护密码、HSM 配置、Flash Bank Swap、LBIST、SSW（Startup Software）等。'));

children.push(H2('1.2 ORIG + COPY 冗余机制'));
children.push(P('为提高安全性与抗损坏能力，几乎所有关键 UCB 都采用 ORIG（原始）+ COPY（副本）双备份结构：芯片复位时启动 ROM 同时读取两份内容并进行一致性比对。'));
children.push(Bullet('ORIG 与 COPY 在地址上呈对称布局（间隔 0x1000）'));
children.push(Bullet('两者必须保持完全一致，否则启动 ROM 视为无效或回退到默认行为'));
children.push(Bullet('编程时务必同步写入 ORIG 和 COPY，并确保 CONFIRMATION code 一致'));

// =============== 2. UCB 地址映射表 ===============
children.push(H1('2. UCB 地址映射表'));
children.push(P('下表列出了 TC3xx 全部 48 个 UCB sector 的功能映射。地址列以 16 进制表示，所有 sector 的尺寸均为 512 字节（0x200）。'));

const ucbHeaders = ['Sector', '地址范围', '名称', '用途说明'];
const ucbWidths = [10, 28, 24, 38];
const ucbRows = [
    ['0–3',   '0xAF400000 – 0xAF4007FF', 'BMHD0–3',          '启动模式头（复位向量、启动地址、CRC）'],
    ['4',     '0xAF400800 – 0xAF4009FF', 'UCB_OTP',          '一次性编程位（OTP）'],
    ['5',     '0xAF400A00 – 0xAF400BFF', 'UCB_DFLASH',       'DFlash 读/写保护密码'],
    ['6',     '0xAF400C00 – 0xAF400DFF', 'UCB_DBG',          '调试接口保护密码（DAP）'],
    ['7',     '0xAF400E00 – 0xAF400FFF', 'UCB_HSM',          'HSM 硬件安全模块配置'],
    ['8–11',  '0xAF401000 – 0xAF4017FF', 'BMHD0–3_COPY',     'BMHD 备份副本'],
    ['12–15', '0xAF401800 – 0xAF401FFF', '保护区 COPY',       'OTP / DFLASH / DBG / HSM 备份'],
    ['16–17', '0xAF402000 – 0xAF4023FF', 'UCB_PFLASH',       'PFlash 扇区读/写保护配置'],
    ['18–19', '0xAF402400 – 0xAF4027FF', 'UCB_PFLASH_COPY',  'PFlash 保护备份'],
    ['20–23', '0xAF402800 – 0xAF402FFF', 'UCB_SWAP',         'PFlash 地址交换（A/B bank swap）'],
    ['24–27', '0xAF403000 – 0xAF4037FF', 'UCB_SWAP_COPY',    'Swap 备份'],
    ['28–31', '0xAF403800 – 0xAF403FFF', 'UCB_LBIST',        '逻辑内建自测试配置'],
    ['32–35', '0xAF404000 – 0xAF4047FF', 'UCB_LBIST_COPY',   'LBIST 备份'],
    ['36–39', '0xAF404800 – 0xAF404FFF', 'UCB_SSW',          '启动软件（Startup Software）配置'],
    ['40–43', '0xAF405000 – 0xAF4057FF', 'UCB_SSW_COPY',     'SSW 备份'],
    ['44–47', '0xAF405800 – 0xAF405FFF', 'Reserved',         '保留'],
];
children.push(buildTable(ucbHeaders, ucbRows, ucbWidths));
children.push(P(' '));
children.push(Note('凡是带 _COPY 后缀的 sector 必须与对应 ORIG 区域保持完全一致，否则芯片将视该配置无效。'));

// =============== 3. CONFIRMATION Code ===============
children.push(H1('3. CONFIRMATION Code 机制'));
children.push(P('CONFIRMATION code 是每个 UCB sector 末尾用于标记配置生效状态的关键字段。芯片复位过程中，启动 ROM 通过读取 ORIG 与 COPY 的 CONFIRMATION code 决定该 UCB 是被忽略、可重新编程还是已确认锁定。'));

children.push(H2('3.1 字段位置'));
children.push(RichP([
    { text: '位置：每个 UCB sector 内 offset ' },
    { text: '0x1F0', code: true },
    { text: '（即最后 16 字节中的前 4 字节，小端序存储）' },
]));

children.push(H2('3.2 三种状态值'));
const cfHeaders = ['CONFIRMATION 值', '状态', '行为'];
const cfWidths = [25, 20, 55];
const cfRows = [
    ['0x00000000', 'ERASED',    '已擦除/未编程，芯片忽略此 sector，使用默认行为'],
    ['0x43211234', 'UNLOCKED',  '已编程但未锁定，配置生效，且后续仍可重新擦写'],
    ['0x57B5327F', 'CONFIRMED', '已确认锁定，配置生效；需密码解锁才能修改'],
];
children.push(buildTable(cfHeaders, cfRows, cfWidths));
children.push(P(' '));
children.push(Note('上述三个 CONFIRMATION code 值在 AURIX TC2xx / TC3xx 全系列中保持一致，可视为通用规范。'));

children.push(H2('3.3 ORIG + COPY 双重校验'));
children.push(Bullet('ORIG 和 COPY 的 CONFIRMATION code 同时为有效值（UNLOCKED/CONFIRMED）时，配置生效'));
children.push(Bullet('两者均为 0x00000000 时，sector 视为未编程并被忽略'));
children.push(Bullet('两者不一致或损坏时，启动 ROM 通常忽略当前 UCB 并继续后续启动流程'));

// =============== 4. BMHD 详解 ===============
children.push(H1('4. BMHD（Boot Mode Header）详解'));
children.push(P('BMHD 决定芯片复位后的启动地址、启动模式与初始配置。TC3xx 提供 4 个 BMHD（BMHD0–3）及对应的 4 个 BMHD_COPY 副本，共 8 个 sector。'));

children.push(H2('4.1 BMHD 数据结构（512 字节）'));
const bmHeaders = ['偏移 (Offset)', '字段', '宽度', '说明'];
const bmWidths = [16, 22, 12, 50];
const bmRows = [
    ['0x000', 'STAD',            '32-bit', '启动地址（Start Address，CPU0 复位后的取指地址）'],
    ['0x004', 'BMI',             '16-bit', 'Boot Mode Index，启动模式索引'],
    ['0x006', 'BMHDID',          '16-bit', 'BMHD 标识（固定魔数）'],
    ['0x008', 'CRCBMHD',         '32-bit', '数据完整性校验 CRC'],
    ['0x00C', 'CRCBMHD_N',       '32-bit', 'CRCBMHD 的反码（双重校验）'],
    ['0x00E', 'PWx (可选)',       '—',     '密码字段、附加安全参数'],
    ['0x1F0', 'CONFIRMATION',    '32-bit', 'CONFIRMATION code（详见第 3 章）'],
    ['0x1F4', 'ECC / Reserved',  '—',     '保留 / ECC 字段'],
];
children.push(buildTable(bmHeaders, bmRows, bmWidths));

children.push(H2('4.2 BMHD 优先级与切换'));
children.push(Bullet('启动 ROM 按编号 BMHD0 → BMHD1 → BMHD2 → BMHD3 依次评估有效性'));
children.push(Bullet('第一个 ORIG/COPY 一致且 CRC 正确、CONFIRMATION 为 UNLOCKED/CONFIRMED 的 BMHD 即被采用'));
children.push(Bullet('多个 BMHD 可用于实现 A/B 启动、双映像回滚、安全启动等机制'));

children.push(H2('4.3 有效 BMHD 的判定条件'));
children.push(Bullet('CONFIRMATION code 为 UNLOCKED (0x43211234) 或 CONFIRMED (0x57B5327F)'));
children.push(Bullet('CRCBMHD 与 CRCBMHD_N 互为反码且校验通过'));
children.push(Bullet('BMHDID 字段匹配芯片预期魔数'));
children.push(Bullet('ORIG 与 COPY 内容完全一致'));

// =============== 5. 安全保护区详解 ===============
children.push(H1('5. 安全保护区详解'));

children.push(H2('5.1 UCB_OTP — 一次性编程位'));
children.push(Bullet('一旦 CONFIRMATION 被写入 CONFIRMED (0x57B5327F)，配置永不可逆'));
children.push(Bullet('用于固化生产期参数（如客户 ID、生产追踪信息）'));
children.push(Bullet('量产前严禁随意写入 CONFIRMED'));

children.push(H2('5.2 UCB_DFLASH — DFlash 保护密码'));
children.push(Bullet('设置 DFlash 的读保护与写保护密码'));
children.push(Bullet('CONFIRMED 后访问 DFlash 必须先通过密码认证'));
children.push(Bullet('密码丢失意味着 DFlash 被永久锁死'));

children.push(H2('5.3 UCB_DBG — 调试接口保护密码'));
children.push(Bullet('限制 JTAG / DAP（Device Access Port）访问'));
children.push(Bullet('支持多级保护：完全开放、密码访问、永久关闭'));
children.push(Bullet('量产芯片通常应启用，开发阶段保持出厂默认（全 0）即可'));

children.push(H2('5.4 UCB_HSM — HSM 配置'));
children.push(Bullet('使能并配置 HSM 硬件安全模块'));
children.push(Bullet('包括 HSM 启动地址、Boot ROM 配置、密钥保护等'));
children.push(Bullet('错误配置可能导致 HSM 死锁，请严格按照 Infineon 官方手册操作'));

children.push(H2('5.5 出厂默认状态'));
children.push(Bullet('UCB_OTP / UCB_DFLASH / UCB_DBG / UCB_HSM：全 0x00（未编程）'));
children.push(Bullet('调试接口完全开放，可自由读写 Flash'));
children.push(Bullet('CONFIRMATION code 全部为 0x00000000（ERASED）'));

children.push(H2('5.6 CONFIRMED 状态的影响'));
children.push(Note('一旦保护区被写入 CONFIRMED 状态，必须使用密码才能解锁；密码错误次数超限将导致芯片永久锁死，请慎重操作。'));

// =============== 6. UCB 操作命令 ===============
children.push(H1('6. UCB 操作命令（dflash 工具）'));
children.push(P('dflash.exe 是配套的 UCB 操作工具，提供读取、写入、擦除三类子命令。所有 UCB 操作均通过 ucb 子命令进入。'));

children.push(H2('6.1 读取 UCB'));
children.push(...CodeBlock([
    '# 读取全部 UCB（24 KB）',
    'dflash ucb read',
    '',
    '# 读取指定地址范围',
    'dflash ucb read --addr AF400000 --length 200',
    '',
    '# 保存为 Intel HEX 文件',
    'dflash ucb read -a AF400000 -l 200 -o ucb.hex',
]));

children.push(H2('6.2 写入 UCB'));
children.push(...CodeBlock([
    '# 写入 HEX 文件（地址包含在 HEX 中）',
    'dflash ucb write --file ucb_data.hex',
    '',
    '# 写入并验证',
    'dflash ucb write --file ucb_data.hex --verify',
    '',
    '# 写入二进制文件到指定地址',
    'dflash ucb write --file data.bin --addr AF400000',
]));

children.push(H2('6.3 擦除 UCB'));
children.push(...CodeBlock([
    '# 擦除 BMHD0–3（4 个 sector）',
    'dflash ucb erase --addr AF400000 --sectors 4',
    '',
    '# 擦除 BMHD 副本并验证',
    'dflash ucb erase --addr AF401000 --sectors 4 --verify',
]));

children.push(H2('6.4 参数说明'));
const paramHeaders = ['参数', '简写', '类型', '说明'];
const paramWidths = [22, 12, 18, 48];
const paramRows = [
    ['--addr',    '-a', '十六进制地址', 'UCB 起始地址（0x 前缀可选）'],
    ['--length',  '-l', '十六进制长度', '操作字节数'],
    ['--sectors', '-s', '十进制数量',   '擦除的 sector 数（每 sector 512B）'],
    ['--verify',  '-v', '布尔标志',     '操作后回读验证'],
    ['--output',  '-o', '文件路径',     '读取结果输出文件（HEX/BIN）'],
    ['--file',    '-f', '文件路径',     '写入数据来源文件'],
];
children.push(buildTable(paramHeaders, paramRows, paramWidths));

// =============== 7. 安全操作注意事项 ===============
children.push(H1('7. 安全操作注意事项'));

children.push(H2('7.1 锁定区域不可擦除'));
children.push(RichP([
    { text: '以下地址范围属于受保护的锁定区，dflash 工具默认拒绝擦除：' },
]));
children.push(RichP([
    { text: '  • ORIG 区: ' },
    { text: '0xAF400800 – 0xAF400FFF', code: true },
    { text: '（OTP / DFLASH / DBG / HSM）' },
]));
children.push(RichP([
    { text: '  • COPY 区: ' },
    { text: '0xAF401800 – 0xAF401FFF', code: true },
    { text: '（对应 ORIG 的 4 个保护副本）' },
]));

children.push(H2('7.2 操作前必须备份'));
children.push(Bullet('任何写入或擦除前先执行：dflash ucb read -o backup.hex'));
children.push(Bullet('确认备份文件大小与内容正确再继续'));

children.push(H2('7.3 CONFIRMATION code 选择'));
children.push(Bullet('开发阶段强烈推荐使用 UNLOCKED（0x43211234），便于反复修改'));
children.push(Bullet('量产固化时再使用 CONFIRMED（0x57B5327F）'));
children.push(Bullet('未确定的 sector 保持 ERASED（0x00000000）'));

children.push(H2('7.4 致命风险提示'));
children.push(Note('错误的 UCB 写入（CRC 错误、密码错误、HSM 配置错误等）可能导致芯片调试接口关闭、Flash 永久锁死、HSM 死锁，请务必在仿真环境中先行验证。'));
children.push(Note('ORIG 与 COPY 必须保持完全一致；任何不一致都会让 UCB 失效，并可能触发安全策略锁芯片。'));

// =============== 8. 典型工作流程 ===============
children.push(H1('8. 典型工作流程'));

children.push(H2('8.1 新板首次编程 BMHD'));
children.push(Bullet('① 确认 UCB 全区为出厂默认（dflash ucb read 后检查全 0xFF / 0x00）'));
children.push(Bullet('② 准备 BMHD0 数据：填写 STAD、BMI、BMHDID、CRCBMHD/CRCBMHD_N'));
children.push(Bullet('③ 在 offset 0x1F0 写入 CONFIRMATION = 0x43211234（UNLOCKED）'));
children.push(Bullet('④ 同步生成 BMHD0_COPY（地址 0xAF401000）相同内容'));
children.push(Bullet('⑤ 执行：dflash ucb write --file bmhd0.hex --verify'));
children.push(Bullet('⑥ 复位芯片，确认从 STAD 启动成功'));

children.push(H2('8.2 修改 UNLOCKED 状态的 BMHD'));
children.push(Bullet('① dflash ucb read -a AF400000 -l 200 -o old_bmhd.hex（备份）'));
children.push(Bullet('② dflash ucb erase --addr AF400000 --sectors 1 --verify'));
children.push(Bullet('③ 同步擦除 BMHD0_COPY：dflash ucb erase --addr AF401000 --sectors 1 --verify'));
children.push(Bullet('④ 编辑新 BMHD 内容，保持 CONFIRMATION = 0x43211234'));
children.push(Bullet('⑤ dflash ucb write --file new_bmhd.hex --verify（同步写入 ORIG + COPY）'));

children.push(H2('8.3 读取与备份完整 UCB'));
children.push(...CodeBlock([
    '# 读取整个 UCB 区（24KB）并保存为 HEX',
    'dflash ucb read -a AF400000 -l 6000 -o ucb_full_backup.hex',
    '',
    '# 仅备份 BMHD 及其副本（共 8 sector，4 KB）',
    'dflash ucb read -a AF400000 -l 1000 -o bmhd_orig.hex',
    'dflash ucb read -a AF401000 -l 1000 -o bmhd_copy.hex',
]));

children.push(H2('8.4 验证 UCB 状态'));
children.push(Bullet('使用十六进制查看器打开 HEX 文件，定位每个 sector 的 offset 0x1F0'));
children.push(Bullet('对照第 3.2 节判断 ERASED / UNLOCKED / CONFIRMED'));
children.push(Bullet('校验 ORIG 与 COPY 是否完全一致（推荐使用 diff 工具）'));
children.push(Bullet('量产前进行批量自动化校验，确认所有 CONFIRMATION 处于预期状态'));

// =============== 9. Memtool UCB Handler 操作指南 ===============
children.push(H1('9. Memtool UCB Handler 操作指南'));
children.push(P('Infineon Memtool 提供了图形化的 UCB Handler 界面（TC3 UCB Handler），可以直观地查看和修改各 UCB 区块的配置。相较命令行工具，它具备字段语义自动解析、可视化交互等优势，适合调试与单次配置变更场景。'));

children.push(H2('9.1 UCB Handler 界面概述'));
children.push(RichP([
    { text: '通过 Memtool 菜单 ' },
    { text: 'Target → TC3 UCB Handler', bold: true },
    { text: ' 打开 UCB 配置界面。界面顶部有多个 Tab 页，每个 Tab 对应一个 UCB 功能区：' },
]));

const mtTabHeaders = ['Tab 名称', '对应 UCB', '功能'];
const mtTabWidths = [22, 30, 48];
const mtTabRows = [
    ['UCB_BMHD0',  'Boot Mode Header 0',     '主启动配置（启动地址、BMI）'],
    ['UCB_BMHD1',  'Boot Mode Header 1',     '备用启动配置 1'],
    ['UCB_BMHD2',  'Boot Mode Header 2',     '备用启动配置 2'],
    ['UCB_BMHD3',  'Boot Mode Header 3',     '备用启动配置 3'],
    ['UCB_PFLASH', 'PFlash Protection',      'PFlash 扇区读/写保护配置'],
    ['UCB_DFLASH', 'DFlash Protection',      'DFlash 读/写保护密码'],
    ['UCB_DBG',    'Debug Protection',       '调试接口（DAP）保护密码'],
    ['UCB_OTP',    'One-Time Programmable',  '一次性编程位（不可逆）'],
    ['UCB_ECPRIO', 'ECC Priority',           'ECC 优先级配置'],
];
children.push(buildTable(mtTabHeaders, mtTabRows, mtTabWidths));
children.push(P(' '));

children.push(H2('9.2 BMHD Tab 页详解'));
children.push(P('BMHD 页面分为左右两个区域，分别用于查看当前芯片状态与配置新的写入参数。'));

children.push(H3('左侧 "Current status" — 当前芯片中 BMHD 的实际状态'));
children.push(P('点击 Read 按钮读取当前芯片中的实际值，左侧面板会显示如下字段：'));
children.push(RichP([
    { text: 'DMU_HF_CONFIRM0', code: true },
    { text: '：确认寄存器值（如 ' },
    { text: '0x8200AA55', code: true },
    { text: '）' },
]));
children.push(Bullet('BMI.PINDIS：引脚启动禁用位'));
children.push(Bullet('BMI.HWCFG：硬件配置值'));
children.push(Bullet('BMI.LSENA0：锁步使能位'));
children.push(Bullet('BMI.LBISTENA：LBIST 使能位'));
children.push(Bullet('BMI.CHSWENA：CHSW 使能位'));
children.push(Bullet('IsConfirmed：是否已确认锁定（y/n）'));
children.push(Bullet('ProtDis：保护是否已禁用（y/n）'));
children.push(RichP([
    { text: '  • STAD：启动地址（如 ' },
    { text: '0x00000000', code: true },
    { text: ' = 未编程）' },
]));
children.push(Bullet('OptionMask：选项掩码'));
children.push(Bullet('IsValid：BMHD 是否有效（n = CRC 校验失败或数据无效）'));

children.push(H3('右侧 "New" — 配置新的 BMHD 参数'));
children.push(Bullet('Boot mode：启动模式选择'));
children.push(Bullet('Mode selection by HWCFG pins is disable：禁用硬件引脚启动模式选择', 1));
children.push(Bullet('HWCFG：硬件配置下拉框', 1));
children.push(Bullet('Lockstep monitoring：CPU0–CPU3 锁步监控使能'));
children.push(Bullet('LBIST execution start by SSW is enabled：SSW 启动 LBIST'));
children.push(Bullet('CHSW execution after SSW is disabled：SSW 后 CHSW 执行'));
children.push(Bullet('Start address / ABM header：启动地址（显示 "(invalid)" 表示未配置）'));
children.push(Bullet('BMI：Boot Mode Index 值'));

children.push(H2('9.3 底部操作按钮'));
const mtBtnHeaders = ['按钮', '功能', '说明'];
const mtBtnWidths = [25, 25, 50];
const mtBtnRows = [
    ['Disable lock',         '解除 UCB 锁定', '需输入密码，仅对带密码的 UCB 有效'],
    ['Write configuration',  '写入新配置',     '将右侧面板的配置写入芯片 UCB'],
    ['Erase configuration',  '擦除配置',       '将当前 UCB sector 擦除（恢复全 0x00）'],
    ['Read and save...',     '读取并保存',     '读取 UCB 内容保存为文件（备份）'],
    ['Load and write...',    '加载并写入',     '从文件加载配置并写入 UCB'],
];
children.push(buildTable(mtBtnHeaders, mtBtnRows, mtBtnWidths));
children.push(P(' '));

children.push(H2('9.4 各 Tab 页功能说明'));

children.push(H3('UCB_PFLASH — PFlash 保护配置'));
children.push(Bullet('配置哪些 PFlash 扇区被写保护或读保护'));
children.push(Bullet('可按逻辑扇区粒度设置保护掩码'));
children.push(Bullet('设置 PFlash 保护密码（两组 64-bit）'));

children.push(H3('UCB_DFLASH — DFlash 保护配置'));
children.push(Bullet('设置 DFlash 读保护和写保护'));
children.push(Bullet('配置 DFlash 保护密码'));
children.push(Bullet('CONFIRMED 后需密码才能修改 DFlash 保护状态'));

children.push(H3('UCB_DBG — 调试保护配置'));
children.push(Bullet('设置调试接口（JTAG/DAP）访问密码'));
children.push(Bullet('可选择调试接口开放、密码保护或完全禁用'));
children.push(Bullet('CONFIRMED 后需输入密码才能通过调试器连接芯片'));
children.push(Bullet('开发阶段建议保持 UNLOCKED 状态'));

children.push(H3('UCB_OTP — 一次性编程'));
children.push(Bullet('OTP 位一旦编程无法恢复'));
children.push(Bullet('CONFIRMED 后永久锁定，不可逆'));
children.push(Bullet('用于生产环境中永久禁用某些功能（如禁用调试接口）'));
children.push(Note('极度危险操作，编程前务必确认！UCB_OTP 的 Write configuration 是不可逆操作。'));

children.push(H3('UCB_ECPRIO — ECC 优先级'));
children.push(Bullet('配置 ECC 错误处理优先级'));
children.push(Bullet('通常保持默认值即可'));

children.push(H2('9.5 Memtool UCB 操作工作流程'));

children.push(H3('读取当前状态'));
children.push(Bullet('① 打开 TC3 UCB Handler'));
children.push(Bullet('② 选择对应 Tab（如 UCB_BMHD0）'));
children.push(Bullet('③ 点击 "Read" 按钮'));
children.push(Bullet('④ 左侧面板显示当前芯片中的实际值'));

children.push(H3('修改 BMHD 启动配置'));
children.push(Bullet('① 在右侧面板配置新参数（启动地址、BMI 等）'));
children.push(Bullet('② 点击 "Write configuration"'));
children.push(Bullet('③ Memtool 自动执行：擦除 → 写入数据 → 写入 CONFIRMATION code'));
children.push(Bullet('④ 点击 "Read" 验证写入结果'));

children.push(H3('擦除 UCB'));
children.push(Bullet('① 选择要擦除的 UCB Tab'));
children.push(Bullet('② 点击 "Erase configuration"'));
children.push(Bullet('③ 确认操作（擦除后 CONFIRMATION = 0x00000000，芯片忽略此 UCB）'));

children.push(H3('解锁受保护的 UCB'));
children.push(Bullet('① 选择已锁定的 UCB Tab（IsConfirmed = y）'));
children.push(Bullet('② 点击 "Disable lock"'));
children.push(Bullet('③ 输入正确的密码（两组 64-bit）'));
children.push(Bullet('④ 解锁成功后，当前上电周期内可正常擦写'));

children.push(H3('备份与恢复'));
children.push(Bullet('Read and save... — 将 UCB 数据保存为文件（推荐修改前先备份）'));
children.push(Bullet('Load and write... — 从备份文件恢复 UCB 配置'));

children.push(H2('9.6 Memtool vs dflash 工具对比'));
const cmpHeaders = ['功能', 'Memtool', 'dflash 工具'];
const cmpWidths = [22, 38, 40];
const cmpRows = [
    ['界面',      '图形化 GUI',           '命令行 CLI'],
    ['UCB 读取',  'Read 按钮',            'dflash ucb read'],
    ['UCB 写入',  'Write configuration',  'dflash ucb write'],
    ['UCB 擦除',  'Erase configuration',  'dflash ucb erase'],
    ['密码解锁',  'Disable lock 按钮',    '暂不支持'],
    ['配置解析',  '自动解析字段含义',     '原始 hex dump'],
    ['批量操作',  '不支持',               '支持脚本化批量'],
    ['远程操作',  '仅本地',               '支持远程 TAS Server'],
    ['文件备份',  'Read and save',        '--output 参数'],
];
children.push(buildTable(cmpHeaders, cmpRows, cmpWidths));
children.push(P(' '));

children.push(H2('9.7 注意事项'));
children.push(Note('Memtool 和 dflash 工具不能同时连接同一目标设备。'));
children.push(Note('修改 UCB 前务必通过 "Read and save..." 备份当前配置。'));
children.push(Note('UCB_OTP 的 "Write configuration" 是不可逆操作，操作前三思。'));
children.push(Bullet('IsValid = n 表示 BMHD 数据无效（CRC 错误或未编程），芯片会跳过此 BMHD'));
children.push(Bullet('IsConfirmed = n 且 IsValid = n 的 BMHD 不影响芯片启动（被忽略）'));

children.push(P(' '));
children.push(P(' '));
children.push(new Paragraph({
    alignment: AlignmentType.CENTER,
    children: [new TextRun({ text: '— 文档结束 —', italics: true, size: 22, font: FONT_BODY, color: '808080' })],
}));

// ================== 页眉/页脚 ==================
const docHeader = new Header({
    children: [new Paragraph({
        alignment: AlignmentType.RIGHT,
        children: [new TextRun({ text: 'AURIX TC3XX UCB 配置指南', size: 18, font: FONT_HEADING, color: '595959' })],
    })],
});

const docFooter = new Footer({
    children: [new Paragraph({
        alignment: AlignmentType.CENTER,
        children: [
            new TextRun({ text: '第 ', size: 18, font: FONT_BODY, color: '595959' }),
            new TextRun({ children: [PageNumber.CURRENT], size: 18, font: FONT_BODY, color: '595959' }),
            new TextRun({ text: ' 页 / 共 ', size: 18, font: FONT_BODY, color: '595959' }),
            new TextRun({ children: [PageNumber.TOTAL_PAGES], size: 18, font: FONT_BODY, color: '595959' }),
            new TextRun({ text: ' 页', size: 18, font: FONT_BODY, color: '595959' }),
        ],
    })],
});

// ================== 文档组装 ==================
const doc = new Document({
    creator: 'TAS DFlash Erase Tool',
    title: 'AURIX TC3XX UCB 使用说明与配置指南',
    description: 'AURIX TC3xx UCB Reference and Operation Guide',
    styles: {
        default: {
            document: {
                run: { font: FONT_BODY, size: 22 },
            },
        },
    },
    sections: [{
        properties: {
            page: {
                size: {
                    width: convertMillimetersToTwip(210),
                    height: convertMillimetersToTwip(297),
                    orientation: PageOrientation.PORTRAIT,
                },
                margin: {
                    top: convertMillimetersToTwip(25),
                    bottom: convertMillimetersToTwip(25),
                    left: convertMillimetersToTwip(25),
                    right: convertMillimetersToTwip(25),
                },
            },
        },
        headers: { default: docHeader },
        footers: { default: docFooter },
        children,
    }],
});

// ================== 输出 ==================
const outPath = path.join(__dirname, 'AURIX_TC3XX_UCB_Guide.docx');
Packer.toBuffer(doc).then(buf => {
    fs.writeFileSync(outPath, buf);
    console.log('生成成功：' + outPath);
    console.log('文件大小：' + buf.length + ' 字节');
}).catch(err => {
    console.error('生成失败：', err);
    process.exit(1);
});
