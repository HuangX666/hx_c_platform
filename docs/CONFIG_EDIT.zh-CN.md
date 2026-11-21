# 修改 INI、备份配置与应用一组设置

[English](CONFIG_EDIT.md) · [首页](../README.zh-CN.md)

当应用需要按 `[section]` 和 `key` 修改少量 INI 配置项、在覆盖磁盘配置文件前自动保留一份验证过的 `.bak` 备份，或者把增量加载的一组设置合并应用到当前运行期设置对象时，可以使用本页介绍的编辑与备份接口。

你可以先通过第 2 节的完整示例体验保存、备份与显式恢复流程：示例会将 `network.port` 设为字符串 `"8080"` 并写入当前目录的 `preferences.ini`。当你先运行一次生成文件、手动把文件中的端口改为 `9000`、再运行第二次时，新的主文件会更新为 `8080`，而刚才合法的 `9000` 版本会被自动保存进 `preferences.ini.bak`；最后带上 `--restore` 参数运行即可把 `9000` 恢复回主文件。请注意，这是一套**显式调用的保存／恢复流程**，并非后台自动监控恢复。构建目录和运行准备可按[示例指南](../examples/README.zh-CN.md#run)复用。

## 1. 按 section 和 key 修改 INI

- **创建与加载**：调用 `hx_os_ini_create()` 可创建一份空的 INI 配置文档，调用 `hx_os_ini_load()` 则可加载现有的 UTF-8 INI 文件。
- **新增或修改键值**：调用 `hx_os_ini_set(&document, "network", "port", "8080", &error)` 设置字符串键值（若目标 `section` 或 `key` 尚不存在，会自动创建）。底层采用写时复制（Copy-on-Write）替换整棵不可变树：只有当新文档树完整构造成功后，才会更新调用方传入的 `document` 指针；若此前通过 `retain` 保留了旧文档的引用，旧文档及其所有节点依然有效且内容不变。
- **读取与删除**：
  - `hx_os_ini_get(document, section, key, &text, &error)` 将查询到的字符串复制到 `text`（`hx_string`）。
  - `hx_os_ini_remove_key()` 删除单个键，`hx_os_ini_remove_section()` 删除整个小节。
  - 将 `section` 传为 `NULL` 或空字符串 `""` 表示操作顶层全局键；而删除整个小节（`remove_section`）则必须提供非空的节名称。
  - 节名与键名均按字面字节精确匹配且区分大小写（`/`、`.`、`~` 等字符均视为普通字面字符，不作路径展开）。已存在的顶层全局键不能被同名 `[section]` 覆盖，反之亦然。

> [!NOTE]
> **INI 表示边界与持久化时机：**
> INI 中所有值均以字符串存储，不引入自定义数组语法，不自动转换数字，也不保留原文件的注释与空白排版。无法被 INI 语法无损往返表示的内容（例如包含换行符，或包含重读后会被自动裁去的首尾空白）在 `set` 阶段就会被直接拒绝，且失败时原文档保持不变。编辑操作仅在内存中生成新文档树，需要落盘时请继续调用序列化或保存接口。

## 2. 运行完整例子

完整示例 [config_edit.c](../examples/config_edit.c) 会生成并操作如下 INI 文件：

```ini
[network]
host=localhost
port=8080
```

按[示例配置](../examples/README.zh-CN.md#run)准备好依赖与构建目录后，在仓库根目录执行：

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_config_edit
# Linux/macOS：
./build/bin/hx_os_config_edit
# Windows PowerShell：
.\build\bin\Release\hx_os_config_edit.exe
```

程序运行后会打印 `Saved preferences.ini`，并在当前工作目录生成 `preferences.ini`。你可以按以下步骤验证备份与恢复机制（请在测试目录中执行，切勿直接指定生产配置文件）：
1. 首次运行程序，生成 `port=8080` 的 `preferences.ini`。
2. 用文本编辑器把 `preferences.ini` 中的端口改为 `9000`。
3. 再次运行程序：此时主文件 `preferences.ini` 变回 `8080`，同时自动生成的 `preferences.ini.bak` 保留了刚才的 `9000` 版本。
4. 传入 `--restore` 参数运行程序，即可将 `.bak` 中的 `9000` 版本显式恢复回主文件。

在 C++11 中，包含 `<hx/Config.hpp>` 即可使用 `hx::ConfigDocument::emptyIni()`、`setIni()`、`ini()`、`removeIniKey()` 与 `removeIniSection()`：

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

检查返回结果成功后，可调用 `saveBackup("preferences.ini", HX_OS_CONFIG_INI)` 或普通 `save()` 落盘。复制过的 `hx::ConfigDocument` 共享底层旧不可变树，对其中一个对象调用 `setIni()` 会使其指向新树，而不会影响其他副本。全过程不使用 C++ 异常。

## 3. 带备份保存具体保证什么

`hx_os_config_save_backup()` 适用于所有支持序列化的配置格式（JSON、YAML、XML、INI、TOML、ENV、properties），不仅限于 INI。它的执行顺序是：
1. 先在内存中完成新文档的序列化；
2. 若磁盘上已存在主文件，则在配置的容量上限内读取并解析验证现有主文件；
3. 确认现有主文件合法后，将其**原始字节**原子发布到 `原路径 + ".bak"`，随后再将新文档原子发布到主文件路径（调用期间，调用方需确保没有其他线程或进程并发修改这两个路径）。

| 现场情况 | `hx_os_config_save_backup()` 的行为 |
| --- | --- |
| 主文件尚不存在 | 直接创建并写入新主文件；若此前已有旧的 `.bak` 文件则保持不动 |
| 现有主文件合法且未超限 | 先将现有主文件的原始字节原子保存至 `.bak`，再发布新主文件 |
| 现有主文件已损坏或超出读取上限 | 立即返回失败，**主文件与现有 `.bak` 均不替换**（防止用损坏的主文件冲掉原本可用的旧备份） |
| 发布 `.bak` 备份失败 | 立即返回失败，主文件保持不变 |
| `.bak` 发布成功，但随后新主文件发布失败 | 保留刚发布的 `.bak` 备份并返回失败状态（不会自动回滚） |

- **发布详情与断电边界**：可选输出结构 `hx_os_config_backup_result` 会分别记录 `backup` 与 `primary` 的发布状态及父目录同步结果。底层原子写文件操作会先刷盘文件本身，再尝试同步父目录（Windows 平台目前明确报告不支持目录级句柄同步）。注意：两个文件的先后更新不构成跨文件的断电事务。
- **显式从备份恢复（`hx_os_config_restore_backup`）**：读取 `path + ".bak"` 并验证其能够成功解析后，将 `.bak` 的原始字节原子发布回主文件路径。若 `.bak` 不存在、过大或存在语法错误，则拒绝恢复并保持主文件与 `.bak` 原样不变。普通的 `hx_os_config_load()` 绝不会在背地里自动读取 `.bak`。C++11 可调用 `hx::ConfigDocument::restoreBackup(path, format)`，成功时直接返回解析好的恢复文档。

## 4. 把一组设置应用到现有设置

对于运行期的可变设置容器（`hx_settings`），可以使用 C 接口 `hx_settings_apply(current, incoming, &applied, &error)` 或 C++11 接口 `current.apply(incoming)` 将另一份设置中的显式键值批量合并进来：

- **只合并显式设置的值**：函数会先获取 `incoming` 中所有**显式设置过的值**的快照（忽略 `incoming` 中的默认值），而 `current` 中未被提及的键及其原有默认值均保持不变。例如 `current` 原有 `{port: 8080, title: "app"}`，`incoming` 含有 `{port: 9000, retries: 3}`，应用后 `current` 变为 `{port: 9000, title: "app", retries: 3}`。
- **子对象与数组整项替换**：若某个键的值是嵌套对象或数组，会直接整项替换目标键的值，而不是递归合并内部成员。自定义用户指针（User Pointer）继续保持借用，其余类型的值会被深拷贝到目标设置对象的内存上下文中。
- **提交流程与观察者通知**：按字典序（字节序）逐键提交并触发对应键的观察者回调；若中途某个键提交失败，操作立即停止，此前已成功提交的键保持生效，并通过输出参数 `applied`（或 C++ `Result` 携带的计数值）报告已成功应用的键数量（与 `Settings::update()` 契约一致）。如果手上已有提取好的设置值快照，也可直接调用 `hx_settings_apply_value()`／`Settings::apply(SettingValue)`（要求根节点必须为对象类型，若传入标量或数组则返回 `HX_OS_TYPE_MISMATCH`）。

## 5. 容量与成本

- **编辑开销**：每次调用 `hx_os_ini_set()` 或删除接口都会构造一棵新的替换树，并复用序列化器的计数遍历校验 INI 格式合法性（无需额外分配序列化文本缓冲区）。因此它非常适合修改少量配置项；若需一次性构造大批量键值，建议直接使用配置文档构建接口（`hx_os_config_create`）或设置快照。
- **备份内存边界**：带备份保存与恢复过程中，旧文件字节、校验解析树与新序列化文本可能短暂共存，各项大小分别受传入的 `hx_os_config_options` 上限约束。所有保存与恢复均为同步文件 I/O，适用于配置持久化场景，切勿放入高频日志热路径。各平台文件系统边界见[平台支持](PLATFORMS.zh-CN.md)。
