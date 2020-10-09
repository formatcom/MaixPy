#include <stdio.h>
#include <string.h>

#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/mpconfig.h"

#include "i2s.h"
#include "Maix_i2s.h"
#include "py_audio.h"
#include "printf.h"


#include "speech_recognizer.h"

#define save_mask 12345
#define size_per_ftr (4 * 1024)
#define page_per_ftr (size_per_ftr / FLASH_PAGE_SIZE)
#define ftr_per_comm 4
#define size_per_comm (ftr_per_comm * size_per_ftr)
#define comm_num 10
#define ftr_total_size (size_per_comm * comm_num)
//#define ftr_end_addr	0x8080000
#define ftr_end_addr (size_per_ftr * ftr_per_comm * comm_num)
#define ftr_start_addr 0 //(ftr_end_addr-ftr_total_size)

#define VAD_fail 1
#define MFCC_fail 2

static const char *TAG = __FILE__;

extern const mp_obj_type_t Maix_i2s_type;
const mp_obj_type_t modules_speech_recognizer_type;

typedef struct _speech_recognizer_obj_t
{
    mp_obj_base_t base;
    atap_tag atap_arg;
    atap_tag user_atap_arg;
    v_ftr_tag ftr;
    sr_status_t sr_status;
    uint16_t VcBuf[VcBuf_Len]; // VcBuf_Len > atap_len
    uint32_t keyword_num;
    uint32_t model_num;
    uint16_t frm_num;
    uint32_t voice_model_len;
    int16_t *p_mfcc_data;
    v_ftr_tag *ftr_save;
    int result;
    valid_tag valid_voice[max_vc_con];
} speech_recognizer_obj_t;

STATIC mp_obj_t sr_record(size_t n_args, const mp_obj_t *args)
{
    mp_printf(&mp_plat_print, "sr_record\r\n");
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t keyword_num = mp_obj_get_int(args[1]);
    mp_int_t model_num = mp_obj_get_int(args[2]);
    if (keyword_num > 10)
    {
        mp_printf(&mp_plat_print, "[MaixPy] keyword_num>10\n");
        return mp_const_false;
    }
    if (model_num > 4)
    {
        mp_printf(&mp_plat_print, "[MaixPy] keyword_num>4\n");
        return mp_const_false;
    }
    mp_printf(&mp_plat_print, "[MAIXPY]: record[%d:%d]\n", keyword_num, model_num);

    Maix_audio_obj_t *record = MP_OBJ_TO_PTR(args[3]);
    
    // input data
    audio_t* input = &record->audio;

    if (input->points != VcBuf_Len) {
        mp_raise_ValueError("record data != speech_recognizer.get_frame_len()");
    }

    for(int i = 0; i < input->points; i += 1) {
        self->VcBuf[i] = (input->buf[i] & 0xffff) + 32768;//left an right channle 16 bit resolution
    }

    noise_atap(self->VcBuf, atap_len, &self->atap_arg);
	
    if (VAD2(self->VcBuf, self->valid_voice, &self->atap_arg) == 1) {
        self->sr_status = SR_ERROR;
    }
        
    get_mfcc(&(self->valid_voice[0]), &self->ftr, &self->atap_arg);
	if(self->ftr.frm_num==0)
	{
		return MFCC_fail;
	}
	
    // addr = ftr_start_addr + keyword_num * size_per_comm + model_num * size_per_ftr;
	// return save_ftr_mdl(&self->ftr, addr);
    return mp_const_true;
}

STATIC mp_obj_t sr_get_model_data_info(size_t n_args, const mp_obj_t *args)
{
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t keyword_num = mp_obj_get_int(args[1]);
    mp_int_t model_num = mp_obj_get_int(args[2]);

    self->frm_num = self->ftr_save[keyword_num * 4 + model_num].frm_num;
    self->p_mfcc_data = self->ftr_save[keyword_num * 4 + model_num].mfcc_dat;
    

    mp_obj_t list = mp_obj_new_list(0, NULL);
    mp_obj_list_append(list, MP_OBJ_NEW_SMALL_INT(self->frm_num));
    return list;
}

STATIC mp_obj_t sr_print_model(size_t n_args, const mp_obj_t *args)
{
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t keyword_num = mp_obj_get_int(args[1]);
    mp_int_t model_num = mp_obj_get_int(args[2]);
    if (keyword_num > 10)
    {
        mp_printf(&mp_plat_print, "[MaixPy] keyword_num>10\n");
        return mp_const_false;
    }
    if (model_num > 4)
    {
        mp_printf(&mp_plat_print, "[MaixPy] keyword_num>4\n");
        return mp_const_false;
    }

    self->frm_num = self->ftr_save[keyword_num * 4 + model_num].frm_num;
    self->p_mfcc_data = self->ftr_save[keyword_num * 4 + model_num].mfcc_dat;
    

    mp_obj_array_t *sr_array = m_new_obj(mp_obj_array_t);
    sr_array->base.type = &mp_type_bytearray;
    sr_array->typecode = BYTEARRAY_TYPECODE;
    sr_array->free = 0;
    sr_array->len = self->voice_model_len;
    sr_array->items = self->p_mfcc_data;

    mp_printf(&mp_plat_print, "[MaixPy] [(%d,%d)|frm_num:%d]\n", keyword_num, model_num, self->frm_num);

    printf("\r\n[%s]-----------------\r\n", __FUNCTION__);
    int16_t *pbuf_16 = (int16_t *)sr_array->items;
    for (int i = 0; i < (sr_array->len); i++)
    {
        if (((i + 1) % 20) == 0)
        {
            printf("%4d  \n", pbuf_16[i]);
            msleep(2);
        }
        else
            printf("%4d  ", pbuf_16[i]);
    }
    printf("\r\n-----------------#\r\n");

    return mp_const_true;
}

// -----------------------------------------------------------------------------
STATIC mp_obj_t sr_get_model_data(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t keyword_num = mp_obj_get_int(args[1]);
    mp_int_t model_num = mp_obj_get_int(args[2]);
    if (keyword_num > 10)
    {
        mp_printf(&mp_plat_print, "[MaixPy] keyword_num>10\n");
        return mp_const_false;
    }
    if (model_num > 4)
    {
        mp_printf(&mp_plat_print, "[MaixPy] model_num>4\n");
        return mp_const_false;
    }

    mp_printf(&mp_plat_print, "[MaixPy] [(%d,%d)|frm_num:%d]\n", keyword_num, model_num, self->frm_num);

    self->frm_num = self->ftr_save[keyword_num * 4 + model_num].frm_num;
    self->p_mfcc_data = self->ftr_save[keyword_num * 4 + model_num].mfcc_dat;
    

    mp_obj_array_t *sr_array = m_new_obj(mp_obj_array_t);
    sr_array->base.type = &mp_type_bytearray;
    sr_array->typecode = BYTEARRAY_TYPECODE;
    sr_array->free = 0;
    sr_array->len = self->voice_model_len;
    sr_array->items = self->p_mfcc_data;

    return sr_array;
}

STATIC mp_obj_t sr_set_threshold(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t n_thl = mp_obj_get_int(args[1]);
    mp_int_t z_thl = mp_obj_get_int(args[2]);
    mp_int_t s_thl = mp_obj_get_int(args[3]);

    atap_tag temp_atap_arg = { 0, n_thl, z_thl, s_thl };
    self->user_atap_arg = temp_atap_arg;

    return mp_const_true;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sr_set_threshold_obj, 2, 4, sr_set_threshold);

STATIC mp_obj_t sr_add_voice_model(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t keyword_num = mp_obj_get_int(args[1]);
    mp_int_t model_num = mp_obj_get_int(args[2]);
    mp_int_t frm_num = mp_obj_get_int(args[3]);
    mp_obj_array_t *sr_array = MP_OBJ_TO_PTR(args[4]);
    self->p_mfcc_data = (int16_t *)sr_array->items;
    self->voice_model_len = sr_array->len;
    self->frm_num = frm_num;
    if (keyword_num > 10)
    {
        mp_printf(&mp_plat_print, "[MaixPy] keyword_num>10\n");
        return mp_const_false;
    }
    if (model_num > 4)
    {
        mp_printf(&mp_plat_print, "[MaixPy] keyword_num>4\n");
        return mp_const_false;
    }
    mp_printf(&mp_plat_print, "[MaixPy] add_voice_model[%d:%d]\n", keyword_num, model_num);
    mp_printf(&mp_plat_print, "[MaixPy] model[frm_num:%d:len:%d]\n", self->frm_num, sr_array->len);
    // mp_printf(&mp_plat_print, "[MaixPy] self[p:0x%X:len:%d]\n", self->p_mfcc_data, self->voice_model_len);
    // mp_printf(&mp_plat_print, "[MaixPy] sr_array[p:0x%X:len:%d]\n", sr_array->items, sr_array->len);

    self->ftr_save[keyword_num * 4 + model_num].save_sign = save_mask;
    self->ftr_save[keyword_num * 4 + model_num].frm_num = self->frm_num;
    for (int i = 0; i < (vv_frm_max * mfcc_num); i++)
        self->ftr_save[keyword_num * 4 + model_num].mfcc_dat[i] = self->p_mfcc_data[i];

    return mp_const_true;
}

// -----------------------------------------------------------------------------
STATIC mp_obj_t sr_init_helper(speech_recognizer_obj_t *self_in, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    //parse paremeter
    enum
    {
        ARG_dev,
    };
    static const mp_arg_t allowed_args[] = {
        {MP_QSTR_dev, MP_ARG_OBJ, {.u_obj = mp_const_none}},
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->voice_model_len = vv_frm_max * mfcc_num;
    atap_tag temp_atap_arg = { 0, 0, 0, 1000 };
    self->user_atap_arg = temp_atap_arg;
    self->result = -1;
    return mp_const_true;
}

STATIC mp_obj_t sr_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    return sr_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
MP_DEFINE_CONST_FUN_OBJ_KW(sr_init_obj, 1, sr_init);

STATIC mp_obj_t sr_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    mp_arg_check_num(n_args, n_kw, 0, 1, true);

    speech_recognizer_obj_t *self = m_new_obj_with_finaliser(speech_recognizer_obj_t);
    self->base.type = &modules_speech_recognizer_type;

    // init instance
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);

    self->ftr_save = (v_ftr_tag *)malloc(20*4 * sizeof(v_ftr_tag));
    if (self->ftr_save == NULL) {
        mp_raise_ValueError("init speech recognizer error\r\n");
    }

    if (mp_const_false == sr_init_helper(self, n_args, args, &kw_args))
        return mp_const_false;

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t sr_get_result(mp_obj_t self_in)
{
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int res = self->result;
    self->result = -1;
    return mp_obj_new_int(res);
}
MP_DEFINE_CONST_FUN_OBJ_1(sr_get_result_obj, sr_get_result);

STATIC mp_obj_t sr_recognize(mp_obj_t self_in, mp_obj_t record_in)
{
    mp_printf(&mp_plat_print, "[MaixPy] recognize...\r\n");
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    Maix_audio_obj_t *record = MP_OBJ_TO_PTR(record_in);
    
    // input data
    audio_t* input = &record->audio;

    if (input->points != VcBuf_Len) {
        mp_raise_ValueError("record data != speech_recognizer.get_frame_len()");
    }

    for(int i = 0; i < input->points; i += 1) {
        self->VcBuf[i] = (input->buf[i] & 0xffff) + 32768;//left an right channle 16 bit resolution
    }

    if (self->sr_status == SR_RECOGNIZER_WORKING) {

        if (VAD2(self->VcBuf, self->valid_voice, &self->atap_arg) == 1) {
            self->sr_status = SR_ERROR;
        } else {
            LOGI(TAG, "vad ok\n");
            //  if (valid_voice[0].end == ((void *)0)) {
            //      *mtch_dis=dis_err;
            //      USART1_LOGI(TAG, "VAD fail ");
            //      return (void *)0;
            //  }

            get_mfcc(&(self->valid_voice[0]), &self->ftr, &self->atap_arg);
            if (self->ftr.frm_num == 0)
            {
                LOGI(TAG, "MFCC fail ");
                self->sr_status = SR_ERROR;
                self->result = -1;
                return mp_obj_new_int(self->sr_status);
            }
            //  for (i = 0; i < ftr.frm_num * mfcc_num; i++) {
            //      if (i % 12 == 0)
            //          LOGI(TAG, "\n");
            //      LOGI(TAG, "%d ", ftr.mfcc_dat[i]);
            //  }
            //  ftr.word_num = valid_voice[0].word_num;
            LOGI(TAG, "MFCC ok\n");

            uint16_t i = 0;
            uint16_t min_comm = 0;
            uint32_t min_dis = dis_max;
            uint32_t cycle0 = read_csr(mcycle);
            for (uint32_t ftr_addr = ftr_start_addr; ftr_addr < ftr_end_addr; ftr_addr += size_per_ftr)
            {
                //  ftr_mdl=(v_ftr_tag*)ftr_addr;
                v_ftr_tag *ftr_mdl = (v_ftr_tag *)(&self->ftr_save[ftr_addr / size_per_ftr]);
                uint32_t cur_dis = ((ftr_mdl->save_sign) == save_mask) ? dtw(ftr_mdl, &self->ftr) : dis_err;
                if ((ftr_mdl->save_sign) == save_mask)
                {
                    mp_printf(&mp_plat_print, "[MaixPy] no. %d, frm_num = %d, save_mask=%d\r\n", i + 1, ftr_mdl->frm_num, ftr_mdl->save_sign);
                    // LOGI(TAG, "cur_dis=%d\n", cur_dis);
                }
                if (cur_dis < min_dis)
                {
                    min_dis = cur_dis;
                    min_comm = i + 1;
                }
                i++;
            }
            uint32_t cycle1 = read_csr(mcycle) - cycle0;
            // mp_printf(&mp_plat_print, "[MaixPy] recg cycle = 0x%08x\r\n", cycle1);
            if (min_comm % 4)
                min_comm = min_comm / ftr_per_comm + 1;
            else
                min_comm = min_comm / ftr_per_comm;

            if (min_dis != dis_err) {
                self->sr_status = SR_RECOGNIZER_FINISH;
                self->result = (int)min_comm;
            }
        }
    }
    return mp_obj_new_int(self->sr_status);
}
MP_DEFINE_CONST_FUN_OBJ_2(sr_recognize_obj, sr_recognize);

// uint8_t* spch_recg(uint16_t *v_dat, uint32_t *mtch_dis)
// {
// 	uint16_t i;
// 	uint32_t ftr_addr;
// 	uint32_t min_dis;
// 	uint16_t min_comm;
// 	uint32_t cur_dis;
// 	v_ftr_tag *ftr_mdl;
	
// 	noise_atap(v_dat, atap_len, &atap_arg);
	
// 	VAD(v_dat, VcBuf_Len, valid_voice, &atap_arg);
// 	if(valid_voice[0].end==((void *)0))
// 	{
// 		*mtch_dis=dis_err;
// 		USART1_printf("VAD fail ");
// 		return (void *)0;
// 	}
	
// 	get_mfcc(&(valid_voice[0]),&ftr,&atap_arg);
// 	if(ftr.frm_num==0)
// 	{
// 		*mtch_dis=dis_err;
// 		USART1_printf("MFCC fail ");
// 		return (void *)0;
// 	}
	
// 	i=0;
// 	min_comm=0;
// 	min_dis=dis_max;
// 	for(ftr_addr=ftr_start_addr; ftr_addr<ftr_end_addr; ftr_addr+=size_per_ftr)
// 	{
// 		ftr_mdl=(v_ftr_tag*)ftr_addr;
// 		//USART1_printf("save_mask=%d ",ftr_mdl->save_sign);
// 		cur_dis=((ftr_mdl->save_sign)==save_mask)?dtw(&ftr,ftr_mdl):dis_err;
// 		//USART1_printf("cur_dis=%d ",cur_dis);
// 		if(cur_dis<min_dis)
// 		{
// 			min_dis=cur_dis;
// 			min_comm=i;
// 		}
// 		i++;
// 	}
// 	min_comm/=ftr_per_comm;
// 	//USART1_printf("recg end ");
// 	*mtch_dis=min_dis;
// 	return (commstr[min_comm].str);
// }

// STATIC mp_obj_t sr_test(mp_obj_t self_in, mp_obj_t record_in)
// {
//         uint16_t i;
//     uint32_t ftr_addr;
//     uint32_t min_dis;
//     uint16_t min_comm;
//     uint32_t cur_dis;
//     v_ftr_tag *ftr_mdl;
//     uint16_t num;
//     uint16_t frame_index;
//     uint32_t cycle0, cycle1;

// get_noise2:
//     frame_index = 0;
//     const int num = atap_len / frame_mov;
//     //wait for finish
//     i2s_rec_flag = 0;
//     while (1)
//     {
//         while (i2s_rec_flag == 0)
//         {
//             if (ide_get_script_status() == false)
//                 return 0;
//         }
//         if (i2s_rec_flag == 1)
//         {
//             for (i = 0; i < frame_mov; i++)
//                 v_dat[frame_mov * frame_index + i] = rx_buf[i];
//         }
//         else
//         {
//             for (i = 0; i < frame_mov; i++)
//                 v_dat[frame_mov * frame_index + i] = rx_buf[i + frame_mov];
//         }
//         i2s_rec_flag = 0;
//         frame_index++;
//         if (frame_index >= num)
//             break;
//     }
//     noise_atap(v_dat, atap_len, &atap_arg);
//     if (atap_arg.s_thl > user_atap_arg.s_thl)
//     {
//         sr_status = SR_GET_NOISEING;
//         mp_printf(&mp_plat_print, "[MaixPy] get noise again...\n");
//         goto get_noise2;
//     }
//     sr_status = SR_RECOGNIZER_WORKING;
//     mp_printf(&mp_plat_print, "[MaixPy] Please speaking...\n");

//     //wait for finish
//     while (i2s_rec_flag == 0)
//     {
//         if (ide_get_script_status() == false)
//             return 0;
//     }
//     if (i2s_rec_flag == 1)
//     {
//         for (i = 0; i < frame_mov; i++)
//             v_dat[i + frame_mov] = rx_buf[i];
//     }
//     else
//     {
//         for (i = 0; i < frame_mov; i++)
//             v_dat[i + frame_mov] = rx_buf[i + frame_mov];
//     }
//     i2s_rec_flag = 0;
//     while (1)
//     {
//         while (i2s_rec_flag == 0)
//         {
//             if (ide_get_script_status() == false)
//                 return 0;
//         }
//         if (i2s_rec_flag == 1)
//         {
//             for (i = 0; i < frame_mov; i++)
//             {
//                 v_dat[i] = v_dat[i + frame_mov];
//                 v_dat[i + frame_mov] = rx_buf[i];
//             }
//         }
//         else
//         {
//             for (i = 0; i < frame_mov; i++)
//             {
//                 v_dat[i] = v_dat[i + frame_mov];
//                 v_dat[i + frame_mov] = rx_buf[i + frame_mov];
//             }
//         }
//         i2s_rec_flag = 0;
//         if (VAD2(v_dat, valid_voice, &atap_arg) == 1)
//             break;
//         if (receive_char == 's')
//         {
//             *mtch_dis = dis_err;
//             LOGI(TAG, "send 'c' to start\n");
//             return 0;
//         }
//     }

//     // LOGI(TAG, "vad ok\n");
//     //  if (valid_voice[0].end == ((void *)0)) {
//     //      *mtch_dis=dis_err;
//     //      USART1_LOGI(TAG, "VAD fail ");
//     //      return (void *)0;
//     //  }

//     get_mfcc(&(valid_voice[0]), &ftr, &atap_arg);
//     if (ftr.frm_num == 0)
//     {
//         *mtch_dis = dis_err;
//         LOGI(TAG, "MFCC fail ");
//         return 0;
//     }
//     //  for (i = 0; i < ftr.frm_num * mfcc_num; i++) {
//     //      if (i % 12 == 0)
//     //          LOGI(TAG, "\n");
//     //      LOGI(TAG, "%d ", ftr.mfcc_dat[i]);
//     //  }
//     //  ftr.word_num = valid_voice[0].word_num;
//     LOGI(TAG, "MFCC ok\n");
//     i = 0;
//     min_comm = 0;
//     min_dis = dis_max;
//     cycle0 = read_csr(mcycle);
//     for (ftr_addr = ftr_start_addr; ftr_addr < ftr_end_addr; ftr_addr += size_per_ftr)
//     {
//         //  ftr_mdl=(v_ftr_tag*)ftr_addr;
//         v_ftr_tag *ftr_mdl = (v_ftr_tag *)(&ftr_save[ftr_addr / size_per_ftr]);
//         cur_dis = ((ftr_mdl->save_sign) == save_mask) ? dtw(ftr_mdl, &ftr) : dis_err;
//         if ((ftr_mdl->save_sign) == save_mask)
//         {
//             mp_printf(&mp_plat_print, "[MaixPy] no. %d, frm_num = %d, save_mask=%d\r\n", i + 1, ftr_mdl->frm_num, ftr_mdl->save_sign);
//             // LOGI(TAG, "cur_dis=%d\n", cur_dis);
//         }
//         if (cur_dis < min_dis)
//         {
//             min_dis = cur_dis;
//             min_comm = i + 1;
//         }
//         i++;
//     }
//     cycle1 = read_csr(mcycle) - cycle0;
//     // mp_printf(&mp_plat_print, "[MaixPy] recg cycle = 0x%08x\r\n", cycle1);
//     if (min_comm % 4)
//         min_comm = min_comm / ftr_per_comm + 1;
//     else
//         min_comm = min_comm / ftr_per_comm;

//     *mtch_dis = min_dis;
//     return (int)min_comm; //(commstr[min_comm].intst_tr);
// }
// MP_DEFINE_CONST_FUN_OBJ_1(sr_test_obj, sr_test);

STATIC mp_obj_t sr_get_status(mp_obj_t self_in)
{
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->sr_status);
}
MP_DEFINE_CONST_FUN_OBJ_1(sr_get_status_obj, sr_get_status);

STATIC mp_obj_t sr_get_frame_len(mp_obj_t self_in)
{
    return mp_obj_new_int(VcBuf_Len);
}
MP_DEFINE_CONST_FUN_OBJ_1(sr_get_frame_len_obj, sr_get_frame_len);

STATIC mp_obj_t sr_get_noise_len(mp_obj_t self_in)
{
    return mp_obj_new_int(atap_len);
}
MP_DEFINE_CONST_FUN_OBJ_1(sr_get_noise_len_obj, sr_get_noise_len);

STATIC mp_obj_t sr_detect_noise(mp_obj_t self_in, mp_obj_t record_in)
{
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    Maix_audio_obj_t *record = MP_OBJ_TO_PTR(record_in);
    
    // input data
    audio_t* input = &record->audio;

    if (input->points != atap_len) {
        mp_raise_ValueError("record data != speech_recognizer.get_noise_len()");
    }

    for(int i = 0; i < input->points; i += 1) {
        self->VcBuf[i] = (input->buf[i] & 0xffff) + 32768;//left an right channle 16 bit resolution
    }
    noise_atap(self->VcBuf, atap_len, &self->atap_arg);
    if (self->atap_arg.s_thl > self->user_atap_arg.s_thl) {
        self->sr_status = SR_GET_NOISEING;
    } else {
        self->sr_status = SR_RECOGNIZER_WORKING;
    }
    return mp_obj_new_int(self->sr_status);
}
MP_DEFINE_CONST_FUN_OBJ_2(sr_detect_noise_obj,sr_detect_noise);

STATIC mp_obj_t sr_del(mp_obj_t self_in) {
    speech_recognizer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->ftr_save != NULL) {
        free(self->ftr_save);
        self->ftr_save = NULL;
    }
    m_del_obj(speech_recognizer_obj_t, self);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sr_del_obj, sr_del);

MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sr_record_obj, 4, 4, sr_record);
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sr_print_model_obj, 3, 3, sr_print_model);
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sr_get_model_data_info_obj, 3, 3, sr_get_model_data_info);
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sr_get_model_data_obj, 3, 3, sr_get_model_data);
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sr_add_voice_model_obj, 5, 5, sr_add_voice_model);

STATIC const mp_rom_map_elem_t mp_module_sr_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&sr_del_obj) },
    {MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&sr_init_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_status), MP_ROM_PTR(&sr_get_status_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_result), MP_ROM_PTR(&sr_get_result_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_frame_len), MP_ROM_PTR(&sr_get_frame_len_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_noise_len), MP_ROM_PTR(&sr_get_noise_len_obj)},
    {MP_ROM_QSTR(MP_QSTR_detect_noise), MP_ROM_PTR(&sr_detect_noise_obj)},

    {MP_ROM_QSTR(MP_QSTR_record), MP_ROM_PTR(&sr_record_obj)},

    {MP_ROM_QSTR(MP_QSTR_print_model), MP_ROM_PTR(&sr_print_model_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_model_data), MP_ROM_PTR(&sr_get_model_data_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_model_data), MP_ROM_PTR(&sr_get_model_data_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_model_info), MP_ROM_PTR(&sr_get_model_data_info_obj)},

    {MP_ROM_QSTR(MP_QSTR_add_voice_model), MP_ROM_PTR(&sr_add_voice_model_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_threshold), MP_ROM_PTR(&sr_set_threshold_obj)},

    { MP_ROM_QSTR(MP_QSTR_ERROR), MP_ROM_INT(SR_ERROR) },
    { MP_ROM_QSTR(MP_QSTR_RECOGNIZER_WORKING), MP_ROM_INT(SR_RECOGNIZER_WORKING) },
    { MP_ROM_QSTR(MP_QSTR_RECOGNIZER_FINISH), MP_ROM_INT(SR_RECOGNIZER_FINISH) },
    { MP_ROM_QSTR(MP_QSTR_GET_NOISEING), MP_ROM_INT(SR_GET_NOISEING) },

    {MP_ROM_QSTR(MP_QSTR_recognize), MP_ROM_PTR(&sr_recognize_obj)},
};

MP_DEFINE_CONST_DICT(mp_module_sr_locals_dict, mp_module_sr_locals_dict_table);

const mp_obj_type_t modules_speech_recognizer_type = {
    .base = {&mp_type_type},
    .name = MP_QSTR_SpeechRecognizer,
    // .print = sr_print,
    .make_new = sr_make_new,
    .locals_dict = (mp_obj_dict_t *)&mp_module_sr_locals_dict,
};