/*******************************************************************************
 * Copyright (C) Gallium Studio LLC. All rights reserved.
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
#include "UserBtnInterface.h"
#include "UserBtn.h"

FW_DEFINE_THIS_FILE("UserBtn.cpp")

namespace APP {

static char const * const timerEvtName[] = {
    "STATE_TIMER",
};

static char const * const internalEvtName[] = {
    "BTN_TRIG",
    "BTN_UP",
    "BTN_DOWN",
};

static char const * const interfaceEvtName[] = {
    "USER_BTN_START_REQ",
    "USER_BTN_START_CFM",
    "USER_BTN_STOP_REQ",
    "USER_BTN_STOP_CFM",
    "USER_BTN_UP_IND",
    "USER_BTN_DOWN_IND",
};

// User Btn uses PA.12 (i.e. EXTI12)
void UserBtn::ConfigGpioInt() {
    GPIO_InitTypeDef   GPIO_InitStructure;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitStructure.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Pin = GPIO_PIN_12;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    NVIC_SetPriority(EXTI15_10_IRQn, EXTI15_10_PRIO);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void UserBtn::EnableGpioInt() {
    QF_CRIT_STAT_TYPE crit;
    QF_CRIT_ENTRY(crit);
    EXTI->IMR1 = BIT_SET(EXTI->IMR1, GPIO_PIN_12, 0);
    QF_CRIT_EXIT(crit);
}

void UserBtn::DisableGpioInt() {
    QF_CRIT_STAT_TYPE crit;
    QF_CRIT_ENTRY(crit);
    EXTI->IMR1 = BIT_CLR(EXTI->IMR1, GPIO_PIN_12, 0);
    QF_CRIT_EXIT(crit);
}

void UserBtn::GpioIntCallback(Hsmn hsmn) {
    static Sequence counter = 0; 
    Evt *evt = new Evt(UserBtn::BTN_TRIG, hsmn, HSM_UNDEF, counter++);
    Fw::Post(evt);
    DisableGpioInt();
}

UserBtn::UserBtn() :
    Active((QStateHandler)&UserBtn::InitialPseudoState, USER_BTN, "USER_BTN",
           timerEvtName, ARRAY_COUNT(timerEvtName),
           internalEvtName, ARRAY_COUNT(internalEvtName),
           interfaceEvtName, ARRAY_COUNT(interfaceEvtName)),
    m_client(HSM_UNDEF),
    m_stateTimer(GetHsm().GetHsmn(), STATE_TIMER) {}

QState UserBtn::InitialPseudoState(UserBtn * const me, QEvt const * const e) {
    (void)e;
    return Q_TRAN(&UserBtn::Root);
}

QState UserBtn::Root(UserBtn * const me, QEvt const * const e) {
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
            status = Q_TRAN(&UserBtn::Stopped);
            break;
        }
        case USER_BTN_START_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new UserBtnStartCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_STATE, GET_HSMN());
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

QState UserBtn::Stopped(UserBtn * const me, QEvt const * const e) {
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
        case USER_BTN_STOP_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new UserBtnStopCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            status = Q_HANDLED();
            break;
        }
        case USER_BTN_START_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            me->m_client = req.GetFrom();
            Evt *evt = new UserBtnStartCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            status = Q_TRAN(&UserBtn::Started);
            break;
        }
        default: {
            status = Q_SUPER(&UserBtn::Root);
            break;
        }
    }
    return status;
}

QState UserBtn::Started(UserBtn * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            ConfigGpioInt();            
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            DisableGpioInt();
            status = Q_HANDLED();
            break;
        }
        case Q_INIT_SIG: {
            status = Q_TRAN(&UserBtn::Up);
            break;
        }
        case USER_BTN_STOP_REQ: {
            EVENT(e);
            Evt const &req = EVT_CAST(*e);
            Evt *evt = new UserBtnStopCfm(req.GetFrom(), GET_HSMN(), req.GetSeq(), ERROR_SUCCESS);
            Fw::Post(evt);
            status = Q_TRAN(&UserBtn::Stopped);
            break;
        }
        default: {
            status = Q_SUPER(&UserBtn::Root);
            break;
        }
    }
    return status;
}

QState UserBtn::Up(UserBtn * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            Evt *evt = new Evt(USER_BTN_UP_IND, me->m_client, GET_HSMN(), GEN_SEQ());
            Fw::Post(evt);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case BTN_TRIG: {
            EVENT(e);
            EnableGpioInt();
            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) == GPIO_PIN_RESET) {
                Evt *evt = new Evt(BTN_DOWN, GET_HSMN(), GET_HSMN(), GEN_SEQ());
                me->PostSync(evt);
            }
            status = Q_HANDLED();
            break;
        }
        case BTN_DOWN: {
            status = Q_TRAN(&UserBtn::Down);
            break;
        }
        default: {
            status = Q_SUPER(&UserBtn::Started);
            break;
        }
    }
    return status;
}

QState UserBtn::Down(UserBtn * const me, QEvt const * const e) {
    QState status;
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            Evt *evt = new Evt(USER_BTN_DOWN_IND, me->m_client, GET_HSMN(), GEN_SEQ());
            Fw::Post(evt);
            status = Q_HANDLED();
            break;
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            status = Q_HANDLED();
            break;
        }
        case BTN_TRIG: {
            EVENT(e);
            EnableGpioInt();
            if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) != GPIO_PIN_RESET) {
                Evt *evt = new Evt(BTN_UP, GET_HSMN(), GET_HSMN(), GEN_SEQ());
                me->PostSync(evt);
            }
            status = Q_HANDLED();
            break;
        }
        case BTN_UP: {
            status = Q_TRAN(&UserBtn::Up);
            break;
        }
        default: {
            status = Q_SUPER(&UserBtn::Started);
            break;
        }
    }
    return status;
}


/*
QState UserBtn::MyState(UserBtn * const me, QEvt const * const e) {
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
            status = Q_TRAN(&UserBtn::SubState);
            break;
        }
        default: {
            status = Q_SUPER(&UserBtn::SuperState);
            break;
        }
    }
    return status;
}
*/

} // namespace APP
