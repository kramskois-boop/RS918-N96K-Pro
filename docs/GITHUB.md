# Публикация на GitHub (свой репозиторий + CI/Release)

Локальная часть уже подготовлена: дерево чистое, теги `v1.0`/`v1.1`/`v1.2` на месте,
история 4120 коммитов, upstream-проект сохранён как remote `upstream`, Git Credential Manager
(GCM 2.9.1) доступен. Не хватает только **аккаунта и имени репозитория** — после этого всё
делается одной командой (см. §3).

## 1. Что нужно от вас

| Параметр | Пример | Обязательно |
|---|---|---|
| GitHub-логин | `krams` | да |
| Имя репозитория | `RS918-N96K-Pro` (по умолчанию) | нет |
| Видимость | public (рекомендуется: релизы/CI видны и бесплатны) или private | нет |
| Способ входа | OAuth через GCM (рекомендуется), SSH-ключ или PAT | нет |

Пароль от GitHub **не нужен**: используется OAuth-вход GCM (браузер или device-код), токен
хранится в Windows Credential Manager и никогда не попадает в файлы репозитория.

## 2. Создать пустой репозиторий на GitHub

1. <https://github.com/new> → Repository name: `RS918-N96K-Pro`.
2. **Не** добавляйте README, .gitignore и лицензию — репозиторий должен быть пустым.
3. Public/Private — по желанию; для private также работают Actions (с лимитами минут).

## 3. Одна команда: настроить remote, проверить вход, запушить

```powershell
powershell -ExecutionPolicy Bypass -File tools\github-setup.ps1 -User <ваш-логин>
```

Скрипт: проверит чистоту дерева, переименует legacy `origin` (df8oe/UHSDR) в `upstream`,
пропишет `origin = https://github.com/<логин>/RS918-N96K-Pro.git`, проверит доступ
(`git ls-remote`) и **ничего не запушит** без `-Push`. После проверки доступа:

```powershell
powershell -ExecutionPolicy Bypass -File tools\github-setup.ps1 -User <ваш-логин> -Push
```

Скрипт запушит `main` и теги `v1.0`, `v1.1`, `v1.2` (остальные локальные теги — история
upstream — остаются локальными). Первый push отправляет ~205 МБ упакованной истории,
это несколько минут; последующие — только новые коммиты.

### Вариант SSH (если предпочитаете ключи)

```powershell
ssh-keygen -t ed25519 -C "RS918 N96K-Pro"   # Enter, путь по умолчанию: %USERPROFILE%\.ssh\id_ed25519
Get-Content $env:USERPROFILE\.ssh\id_ed25519.pub
```

Публичный ключ добавить: GitHub → Settings → SSH and GPG keys → New SSH key. Затем:

```powershell
powershell -ExecutionPolicy Bypass -File tools\github-setup.ps1 -User <ваш-логин> -Protocol ssh -Push
```

Если Windows-служба `ssh-agent` выключена (обычно так), хватает ключа без пароля; при желании
включить агент: `Set-Service ssh-agent -StartupType Manual` (нужны права администратора) —
`Start-Service ssh-agent` и `ssh-add`.

### Вариант PAT (если OAuth недоступен)

Создать fine-grained/classic token со scope `repo` (для push в приватный репозиторий — `repo`;
для правки `.github/workflows/**` — ещё `workflow`). При первом `git ls-remote`/`git push`
GCM спросит логин/пароль: в качестве пароля вставить токен. Токен сохранится в Windows
Credential Manager, не в git config.

## 4. После первого push

1. **Actions**: <https://github.com/<логин>/RS918-N96K-Pro/actions> — должны пройти
   `host-tests` (быстро) и `docker-build` (сборка в pinned GCC 15.2.1, ~1–2 мин после кэша).
   Job `release` запускается только на тег `v*`.
2. **Settings → Actions → General**: разрешить Actions; для job `release` нужен доступ
   `contents: write` (уже прописан в workflow). Если организация/политика ограничивает
   token — разрешить «Read and write permissions» в Workflow permissions.
3. **Ветки/теги**: `main` защищать не обязательно, но полезно: запретить force-push и
   удаление тегов `v*` (Settings → Tags → защита тегов).
4. **Релиз**: изменения прошивки → новый тег, например:

   ```powershell
   git tag -a v1.3 -m "RS918 N96K-Pro v1.3"
   git push origin v1.3
   ```

   Job `release` соберёт артефакты (`tools/release.py`), проверит `SHA256SUMS.txt` и
   опубликует GitHub Release с файлами. Тело релиза — из `docs/release-notes/<tag>.md`,
   если файл есть, иначе из шаблона (шаблон содержит TODO — проверьте перед публикацией).

## 5. Безопасность

* Пароль GitHub и PAT **не** храните в файлах репозитория, remote-URL, чатах и логах.
  GCM хранит секреты в Windows Credential Manager; при компрометации — отозвать токен.
* Не добавляйте PAT в URL вида `https://user:TOKEN@github.com/...` — он попадёт в историю
  команд и `.git/config`.
* `release/`, `build/`, `*.log`, `artifacts/` уже в `.gitignore`; секретов в репозитории нет.
* Смена аккаунта: `git credential-manager github logout <account>` затем
  `git credential-manager github login`.

## 6. Проверка/диагностика

```powershell
git remote -v                          # origin -> ваш репозиторий, upstream -> df8oe/UHSDR
git credential-manager github list     # сохранённые аккаунты
git ls-remote origin | Select-Object -First 5
```

| Симптом | Причина/решение |
|---|---|
| `401/403` на `git ls-remote` | не выполнен вход: `git credential-manager github login` |
| `Repository not found` | опечатка в логине/имени или репозиторий не создан |
| `remote: Permission to ... denied` | нет прав у аккаунта; проверить `github login` |
| Push застрял на «Writing objects» | первый push большой (~205 МБ), это нормально |
| Actions не стартуют | Settings → Actions → General → Allow all actions |

## 7. Что уже сделано локально (этот коммит)

| Настройка | Значение |
|---|---|
| `remote.upstream` | `https://github.com/df8oe/UHSDR.git` (исходный проект, только для справки) |
| `remote.origin` | добавит `tools/github-setup.ps1` после получения логина |
| `user.name` / `user.email` | `N96K-Pro Maintainer` / `maintainer@n96k.pro` (для истории) |
| `core.autocrlf` / `core.longpaths` / `fetch.prune` | `false` / `true` / `true` |
| Credentials | Git Credential Manager 2.9.1 + Windows Credential Manager |
| Теги к отправке | `v1.0`, `v1.1`, `v1.2` |
| Ветка | `main` |

> Локальная настройка не меняет историю и не трогает исходную архив-папку. Скрипт можно
> запускать повторно; он идемпотентен (кроме первого переименования `origin` → `upstream`).
