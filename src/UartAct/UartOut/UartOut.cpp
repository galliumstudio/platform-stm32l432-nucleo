/*******************************************************************************
 * Copyright (C) 2018 Gallium Studio LLC (Lawrence Lo). All rights reserved.
 *
 * This program is open source software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Alternatively, this program may be distributed and modified under the
 * terms of Gallium Studio LLC commercial licenses, which expressly supersede
 * the GNU General Public License and are specifically designed for licensees
 * interested in retaining the proprietary status of their code.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Contact information:
 * Website - https://www.galliumstudio.com
 * Source repository - https://github.com/galliumstudio
 * Email - admin@galliumstudio.com
 ******************************************************************************/

#include "app_hsmn.h"
#include "fw_log.h"
#include "fw_assert.h"
#include "UartAct.h"
#include "UartOutInterface.h"
#include "UartOut.h"

FW_DEFINE_THIS_FILE("UartOut.cpp")

namespace APP {

static char const * const timerEvtName[] = {
    "ACTIVE_TIMER",
};

static char const * const internalEvtName[] = {
    "DONE",
    "DMA_DONE",
    "CONTINUE",
    "HW_FAIL",
};

static char const * const interfaceEvtName[] = {
    "UART_OUT_START_REQ",
    "UART_OUT_START_CFM",
    "UART_OUT_STOP_REQ",
    "UART_OUT_STOP_CFM",
    "UART_OUT_FAIL_IND",
    "UART_OUT_WRITE_REQ",
    "UART_OUT_WRITE_CFM",
    "UART_OUT_EMPTY_IND",
};

static char const * const hsmName[] = {
    "UART2_OUT",
};

static uint16_t GetInst(Hsmn hsmn) {
    uint16_t inst = hsmn - UART_OUT;
    FW_ASSERT(inst < UART_OUT_COUNT);
    return inst;
}

static char const * GetName(Hsmn hsmn) {
    uint16_t inst = GetInst(hsmn);
    FW_ASSERT(inst < ARRAY_COUNT(hsmName));
    return hsmName[inst];
}

void UartOut::DmaCompleteCallback(Hsmn hsmn) {
    static Sequence counter = 10000;
    Evt *evt = new Evt(UartOut::DMA_DONE, hsmn, HSM_UNDEF, counter++);
    Fw::Post(evt);
}

UartOut::UartOut(Hsmn hsmn, UART_HandleTypeDef &hal) :
    Region((QStateHandler)&UartOut::InitialPseudoState, hsmn, GetName(hsmn),
           timerEvtName, ARRAY_COUNT(timerEvtName),
           internalEvtName, ARRAY_COUNT(internalEvtName),
           interfaceEvtName, ARRAY_COUNT(interfaceEvtName)),
    m_hal(hal), m_client(HSM_UNDEF), m_fifo(NULL), m_writeCount(0),
    m_activeTimer(this->GetHsm().GetHsmn(), ACTIVE_TIMER) {}

QState UartOut::InitialPseudoState(UartOut * const me, QEvt const * const e) {
    (void)e;
    return Q_TRAN(&UartOut::Root);
}

QState UartOut::Root(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_INIT_SIG: {
            status = Q_TRAN(&UartOut::Stopped);
            break;
        }
        case UART_OUT_START_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new UartOutStartCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_STATE, GET_HSMN());
            Fw::Post(evt);
            status = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&QHsm::top);
            break;
        }
    }
    return status;
}

QState UartOut::Stopped(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case UART_OUT_STOP_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new UartOutStopCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            status = Q_HANDLED();
            break;
        }
        case UART_OUT_START_REQ: {
            EVENT(e);
            UartOutStartReq const &req = static_cast<UartOutStartReq const &>(*e);
            FW::Active *container = me->GetContainer();
            FW_ASSERT(container);
            FW_ASSERT(req.GetFrom() == container->GetHsm().GetHsmn());
            me->m_fifo = req.GetFifo();
            FW_ASSERT(me->m_fifo);
            me->m_fifo->Reset();
            me->m_client = req.GetClient();
            Evt *evt = new UartOutStartCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            status = Q_TRAN(&UartOut::Started);
            break;
        }
        default: {
            status = Q_SUPER(&UartOut::Root);
            break;
        }
    }
    return status;
}

QState UartOut::Started(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            LOG("Instance = %d", GetInst(GET_HSMN()));
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_INIT_SIG: {
            status = Q_TRAN(&UartOut::Inactive);
            break;
        }
        case UART_OUT_STOP_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new UartOutStopCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            status = Q_TRAN(&UartOut::Stopped);
            break;
        }
        default: {
            status = Q_SUPER(&UartOut::Root);
            break;
        }
    }
    return status;
}

QState UartOut::Inactive(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case UART_OUT_WRITE_REQ: {
            //EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new ErrorEvt(UART_OUT_WRITE_CFM, req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            if (me->m_fifo->GetUsedCount() == 0) {
                status = Q_HANDLED();
            } else {
                status = Q_TRAN(&UartOut::Active);
            }
            break;
        }

        default: {
            status = Q_SUPER(&UartOut::Started);
            break;
        }
    }
    return status;
}

QState UartOut::Active(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            me->m_activeTimer.Start(ACTIVE_TIMEOUT_MS);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_activeTimer.Stop();
            status = Q_HANDLED();
            break;
        }
        case Q_INIT_SIG: {
            status = Q_TRAN(&UartOut::Normal);
            break;
        }
        case CONTINUE: {
            status = Q_TRAN(&UartOut::Active);
            break;
        }
        case DONE: {
            status = Q_TRAN(&UartOut::Inactive);
            break;
        }
        // Gallium - TODO Add HW_FAIL
        case ACTIVE_TIMER: {
            EVENT(e);
            // Need FW:: to avoid from being confused with the state Active.
            FW::Active *container = me->GetContainer();
            FW_ASSERT(container);
            Evt *evt = new UartOutFailInd(container->GetHsm().GetHsmn(), GET_HSMN(), GEN_SEQ(), ERROR_TIMEOUT, GET_HSMN());
            Fw::Post(evt);
            status = Q_TRAN(&UartOut::Failed);
            break;
        }
        default: {
            status = Q_SUPER(&UartOut::Started);
            break;
        }
    }
    return status;
}

QState UartOut::Normal(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            // Zero-copy.
            Fifo &fifo = *(me->m_fifo);
            uint32_t addr = fifo.GetReadAddr();
            uint32_t len = fifo.GetUsedCount();
            if ((addr + len - 1) > fifo.GetMaxAddr()) {
                len = fifo.GetMaxAddr() - addr + 1;
            }
            FW_ASSERT((len > 0) && (len <= fifo.GetUsedCount()));
            // Gallium - Only applicable to STM32F7.
            //SCB_CleanDCache_by_Addr((uint32_t *)(ROUND_DOWN_32(addr)), ROUND_UP_32(addr + len - ROUND_DOWN_32(addr)));
            HAL_UART_Transmit_DMA(&me->m_hal, (uint8_t*)addr, len);
            me->m_writeCount = len;
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case UART_OUT_WRITE_REQ: {
            //EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new ErrorEvt(UART_OUT_WRITE_CFM, req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            status = Q_HANDLED();
            break;
        }
        case DMA_DONE: {
            //EVENT(e);
            me->m_fifo->IncReadIndex(me->m_writeCount);
            Evt *evt;
            if (me->m_fifo->GetUsedCount()) {
                evt = new Evt(CONTINUE, GET_HSMN());
                me->PostSync(evt);
            } else {
                evt = new Evt(UART_OUT_EMPTY_IND, me->m_client, GET_HSMN(), GEN_SEQ());
                Fw::Post(evt);
                evt = new Evt(DONE, GET_HSMN());
                me->PostSync(evt);
            }
            status = Q_HANDLED();
            break;
        }
        case UART_OUT_STOP_REQ: {
            EVENT(e);
            me->GetHsm().Defer(e);
            status = Q_TRAN(&UartOut::StopWait);
            break;
        }
        default: {
            status = Q_SUPER(&UartOut::Active);
            break;
        }
    }
    return status;
}

QState UartOut::StopWait(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->GetHsm().Recall();
            status = Q_HANDLED();
            break;
        }
        case UART_OUT_STOP_REQ: {
            EVENT(e);
            me->GetHsm().Defer(e);
            status = Q_HANDLED();
            break;
        }
        case DMA_DONE: {
            EVENT(e);
            me->m_fifo->IncReadIndex(me->m_writeCount);
            Evt *evt = new Evt(DONE, GET_HSMN());
            me->PostSync(evt);
            status = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&UartOut::Active);
            break;
        }
    }
    return status;
}

QState UartOut::Failed(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        default: {
            status = Q_SUPER(&UartOut::Started);
            break;
        }
    }
    return status;
}

/*
QState UartOut::MyState(UartOut * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case Q_INIT_SIG: {
            status = Q_TRAN(&UartOut::SubState);
            break;
        }
        default: {
            status = Q_SUPER(&UartOut::SuperState);
            break;
        }
    }
    return status;
}
*/

} // namespace APP

