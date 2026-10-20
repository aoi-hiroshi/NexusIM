# NexusIM third-party notices

NexusIM is an educational integration project. Each dependency remains subject
to its own license; this file is a source/usage inventory, not a replacement
for those license texts.

| Component | Version/source used by Stage 7 | License/source note |
|---|---|---|
| Muduo | `chenshuo/muduo` commit `b55b0ac6a10d4ded1301c3172e779ec46ada2ab3` | Upstream BSD-style license; its `License` file is copied into the backend image. |
| nlohmann/json | Embedded single header reports version 3.6.1 | MIT; notice is retained in `chatserver/thirdparty/json.hpp`. |
| Qt | 5.12.8, Windows client | Use under the operator's applicable LGPL/commercial terms; `windeployqt` supplies runtime files. |
| Protobuf | Ubuntu 24.04 distribution package | BSD-3-Clause upstream. |
| OpenSSL | Ubuntu 24.04 distribution package | Apache-2.0 upstream for current releases. |
| Apache ZooKeeper | Official image 3.9.6 and Ubuntu C client | Apache-2.0. |
| Redis | Official image 7.4.11 | Review the license shipped with that image before redistribution. |
| MySQL | Official image 8.4.11 | GPL/commercial terms apply to the server and client components. |
| Nginx | Official image 1.31.7 | BSD-2-Clause upstream. |
| hiredis | Ubuntu 24.04 distribution package | BSD-3-Clause. |
| libcurl | Ubuntu 24.04 distribution package | curl license. |

The original ChatServer and MPRPC course snapshots are attributed to 施磊 in
the repository documentation. The imported snapshot did not contain a
recoverable upstream Git history or an explicit project-wide license. Do not
claim that those snapshots are MIT/BSD or redistribute the project as a
commercially licensed product until the original course repository and license
have been confirmed.
