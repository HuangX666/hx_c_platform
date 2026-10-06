# 修改 INI、备份配置与应用一组设置

[English](CONFIG_EDIT.md) · [首页](../README.zh-CN.md)

## 1. 按 section 和 key 修改 INI

`hx_os_ini_create()` 创建空文档，`hx_os_ini_load()` 加载现有 UTF-8 文件。用 `hx_os_ini_set(&document, "network", "port", "8080", &error)` 设置字符串；缺失的 section、key 会自动创建。只有新文档完整构造成功，才替换调用者持有的指针。如果之前保留了旧文档，旧节点仍然有效，内容不变。

`hx_os_ini_get(document, section, key, &text, &error)` 将结果复制到 `hx_string`。`hx_os_ini_remove_key()` 删除一个键，`hx_os_ini_remove_section()` 删除整个分组。section 为 NULL 或空字符串时表示全局键；删除分组需要非空名称。名称按字面匹配并区分大小写，斜杠、点号、波浪号不代表层级路径。已有全局键不能被同名分组覆盖，反之亦然。

INI 的值保持字符串，不增加数组语法，不自动转换数字，也不保留注释和原排版。现有 INI 格式规则无法往返保存的内容会被拒绝，例如值中包含换行、读回后会消失的首尾空白。失败保留原文档。编辑使用原文档的分配器和容量限制，底层树仍然不可变，保留引用后可共享读取；要修改磁盘文件，再调用序列化或保存接口。

## 2. 运行完整例子

[config_edit.c](../examples/config_edit.c) 会创建以下文件：

```ini
[network]
host=localhost
port=8080
```

在项目根目录执行：

```sh
cmake -S . -B build
cmake --build build --config Release --target hx_os_config_edit
# Linux/macOS：
./build/bin/hx_os_config_edit
# Windows PowerShell：
.\build\bin\Release\hx_os_config_edit.exe
```

程序输出 `Saved preferences.ini`，文件放在当前工作目录。先运行一次，手动把文件中的端口改成 `9000`，再运行一次：新主文件为 `8080`，`preferences.ini.bak` 保留刚才的 `9000` 版本。随后给同一个程序传入 `--restore`，即可明确恢复之前的版本。请在专门的示例目录里演示，不要直接使用业务生产配置。

C++11 包含 `<hx/Config.hpp>`，对应接口为 `ConfigDocument::emptyIni()`、`setIni()`、`ini()`、`removeIniKey()`、`removeIniSection()`。以下完整函数创建一个端口设置：

```cpp
hx::Result<hx::ConfigDocument> network_preferences() noexcept
{
	auto document = hx::ConfigDocument::emptyIni();
	if (!document)
		return hx::Failure(document.error());

	auto changed = document.value().setIni("network", "port", "8080");
	if (!changed)
		return hx::Failure(changed.error());

	return document;
}
```

检查返回值后，调用 `saveBackup("preferences.ini", HX_OS_CONFIG_INI)` 或普通 `save()`。复制过的 `ConfigDocument` 继续保留旧树，修改其中一个便利对象不会修改另一个副本。不使用异常处理。

## 3. 带备份保存具体保证什么

`hx_os_config_save_backup()` 适用于各个已支持的输出格式，不限于 INI。它先序列化新文档，再在规定上限内读取并验证现有主文件。主文件有效时，将它的原始字节原子发布到“原路径加 `.bak`”，然后发布新主文件。调用期间，调用者需要排除对这两个路径的其他修改。

| 情况 | 行为 |
| --- | --- |
| 主文件不存在 | 创建主文件，已有备份不动 |
| 主文件有效 | 先把原始字节保存到 `.bak`，再发布新主文件 |
| 主文件损坏或超过限制 | 返回失败，不替换两个文件，避免覆盖仍可用的旧备份 |
| 备份发布失败 | 主文件不变 |
| 备份成功，但随后主文件发布失败 | 保留已发布的备份并报告失败，不自动回滚 |

可选输出 `hx_os_config_backup_result` 分别记录备份、主文件的发布结果及父目录同步结果。成功表示发布成功，两个文件的更新不构成断电安全的整体事务。底层先同步文件，再尝试同步目录；Windows 目前明确报告目录同步不支持。临时文件清理失败可能留下待恢复文件，沿用已有原子保存契约。

`hx_os_config_restore_backup()` 是显式恢复：读取 `.bak`，解析通过后，将其原始字节发布到主文件。备份无效、过大或不存在时不替换主文件，备份自身也保留。普通 `hx_os_config_load()` 不会自行恢复。C++ 使用 `ConfigDocument::restoreBackup(path, format)`，成功结果带有已经解析的恢复文档，也可通过额外输出查看发布及目录同步情况。

## 4. 把一组设置应用到现有设置

对于已经创建的可变设置对象，C 使用 `hx_settings_apply(current, incoming, &applied, &error)`，C++ 使用 `current.apply(incoming)`。库先取得来源的显式值快照，不复制来源的默认值；没有涉及的目标键、目标默认值继续保留。

例如目标原来有 `{port: 8080, title: "app"}`，来源有 `{port: 9000, retries: 3}`，应用后就是 `{port: 9000, title: "app", retries: 3}`。嵌套对象和数组整项替换，不递归合并成员。用户指针继续借用，其余值复制到目标对象的 context 中。

按键的字节排序逐项提交并通知观察者，首次失败立即停止，已经成功的项目保留。C 的输出参数或 C++ 结果携带的值都会报告成功项数，包括失败时，行为与现有 `Settings::update()` 一致。观察者重入和其他线程的写入仍遵循普通 setter 契约，这不是事务。允许把对象应用到它自己；如果手上已有对象快照，使用 `hx_settings_apply_value()` / `Settings::apply(SettingValue)`，标量或数组根节点会返回 TYPE_MISMATCH。

## 5. 容量与成本

INI 编辑构造一棵替换树，并复用序列化器的计数遍历检查格式，不额外分配序列化文本缓冲。旧引用持有的树可能与新树同时存在。连续逐键修改会反复复制树；大批量构造更适合使用普通配置构造接口或设置快照。

备份和恢复按配置选项限制读取量。旧字节、解析树、新序列化文本可能共存，限制分别作用于文档明确说明的缓冲／树，不是整个进程的总内存配额。这些接口同步执行，定位是配置持久化，不用于日志热路径。设置应用则逐键复制值、复用已有根更新，不增加后台线程。实际平台验证情况见[验证记录](VALIDATION.zh-CN.md)。
