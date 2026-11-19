/*
 *  Copyright (c) 2026 Infineon Technologies AG.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *  **************************************************************************************************************** */

//********************************************************************************************************************
//  GDB Remote Serial Protocol (RSP) stub：把 debug_session(MCD) 暴露给 GDB/IDE。
//  `wiggle gdbserver [--port N]` 监听 TCP，GDB `target remote :N` 连接。
//  借助 GDB 前端获得源码级/反汇编/表达式能力；stub 只负责目标访问。
//********************************************************************************************************************

#ifndef WIGGLE_GDB_STUB_H
#define WIGGLE_GDB_STUB_H

namespace wiggle_dbg {

// 启动 GDB RSP server（阻塞直到客户端断开）；port 默认 3333
int gdbServe(int port);

}  // namespace wiggle_dbg

#endif  // WIGGLE_GDB_STUB_H