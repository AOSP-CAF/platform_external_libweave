// Copyright 2015 The Weave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/device_registration_info.h"

#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/values.h>
#include <gtest/gtest.h>
#include <weave/provider/test/fake_task_runner.h>
#include <weave/provider/test/mock_config_store.h>
#include <weave/provider/test/mock_http_client.h>
#include <weave/test/unittest_utils.h>

#include "src/bind_lambda.h"
#include "src/component_manager_impl.h"
#include "src/http_constants.h"
#include "src/privet/auth_manager.h"
#include "src/test/mock_clock.h"

using testing::_;
using testing::AtLeast;
using testing::HasSubstr;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Return;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;
using testing::SaveArg;
using testing::StrictMock;
using testing::WithArgs;

namespace weave {

using test::CreateDictionaryValue;
using test::CreateValue;
using provider::test::MockHttpClient;
using provider::test::MockHttpClientResponse;
using provider::HttpClient;

namespace {

namespace test_data {

const char kXmppEndpoint[] = "xmpp.server.com:1234";
const char kServiceUrl[] = "http://gcd.server.com/";
const char kOAuthUrl[] = "http://oauth.server.com/";
const char kApiKey[] = "GOadRdTf9FERf0k4w6EFOof56fUJ3kFDdFL3d7f";
const char kClientId[] =
    "123543821385-sfjkjshdkjhfk234sdfsdfkskd"
    "fkjh7f.apps.googleusercontent.com";
const char kClientSecret[] = "5sdGdGlfolGlrFKfdFlgP6FG";
const char kCloudId[] = "4a7ea2d1-b331-1e1f-b206-e863c7635196";
const char kDeviceId[] = "f6885e46-b432-42d7-86a5-d759bfb61f62";
const char kClaimTicketId[] = "RTcUE";
const char kAccessToken[] =
    "ya29.1.AADtN_V-dLUM-sVZ0qVjG9Dxm5NgdS9J"
    "Mx_JLUqhC9bED_YFjzHZtYt65ZzXCS35NMAeaVZ"
    "Dei530-w0yE2urpQ";
const char kRefreshToken[] =
    "1/zQmxR6PKNvhcxf9SjXUrCjcmCrcqRKXctc6cp"
    "1nI-GQ";
const char kRobotAccountAuthCode[] =
    "4/Mf_ujEhPejVhOq-OxW9F5cSOnWzx."
    "YgciVjTYGscRshQV0ieZDAqiTIjMigI";
const char kRobotAccountEmail[] =
    "6ed0b3f54f9bd619b942f4ad2441c252@"
    "clouddevices.gserviceaccount.com";
const char kAuthInfo[] = R"({
  "localAuthInfo": {
    "certFingerprint":
    "FQY6BEINDjw3FgsmYChRWgMzMhc4TC8uG0UUUFhdDz0=",
    "localId": "f6885e46-b432-42d7-86a5-d759bfb61f62"
  }
})";

}  // namespace test_data

std::string GetFormField(const std::string& data, const std::string& name) {
  EXPECT_FALSE(data.empty());
  for (const auto& i : WebParamsDecode(data)) {
    if (i.first == name)
      return i.second;
  }
  return {};
}

std::unique_ptr<HttpClient::Response> ReplyWithJson(int status_code,
                                                    const base::Value& json) {
  std::string text;
  base::JSONWriter::WriteWithOptions(
      json, base::JSONWriter::OPTIONS_PRETTY_PRINT, &text);

  std::unique_ptr<MockHttpClientResponse> response{
      new StrictMock<MockHttpClientResponse>};
  EXPECT_CALL(*response, GetStatusCode())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(status_code));
  EXPECT_CALL(*response, GetContentType())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(http::kJsonUtf8));
  EXPECT_CALL(*response, GetData())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(text));
  return std::move(response);
}

std::pair<std::string, std::string> GetAuthHeader() {
  return {http::kAuthorization,
          std::string("Bearer ") + test_data::kAccessToken};
}

std::pair<std::string, std::string> GetJsonHeader() {
  return {http::kContentType, http::kJsonUtf8};
}

std::pair<std::string, std::string> GetFormHeader() {
  return {http::kContentType, http::kWwwFormUrlEncoded};
}

}  // anonymous namespace

class DeviceRegistrationInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EXPECT_CALL(clock_, Now())
        .WillRepeatedly(Return(base::Time::FromTimeT(1450000000)));
    ReloadDefaults(true);
  }

  void ReloadDefaults(bool allow_endpoints_override) {
    EXPECT_CALL(config_store_, LoadDefaults(_))
        .WillOnce(Invoke([allow_endpoints_override](Settings* settings) {
          settings->client_id = test_data::kClientId;
          settings->client_secret = test_data::kClientSecret;
          settings->api_key = test_data::kApiKey;
          settings->oem_name = "Coffee Pot Maker";
          settings->model_name = "Pot v1";
          settings->name = "Coffee Pot";
          settings->description = "Easy to clean";
          settings->location = "Kitchen";
          settings->local_anonymous_access_role = AuthScope::kViewer;
          settings->model_id = "AAAAA";
          settings->oauth_url = test_data::kOAuthUrl;
          settings->service_url = test_data::kServiceUrl;
          settings->xmpp_endpoint = test_data::kXmppEndpoint;
          settings->allow_endpoints_override = allow_endpoints_override;
          return true;
        }));
    config_.reset(new Config{&config_store_});
    dev_reg_.reset(new DeviceRegistrationInfo{
        config_.get(), &component_manager_, &task_runner_, &http_client_,
        nullptr, &auth_});
    dev_reg_->Start();
  }

  void ReloadSettings(bool registered, bool allow_endpoints_override) {
    base::DictionaryValue dict;
    dict.SetInteger("version", 1);
    if (registered) {
      dict.SetString("refresh_token", test_data::kRefreshToken);
      dict.SetString("cloud_id", test_data::kCloudId);
      dict.SetString("robot_account", test_data::kRobotAccountEmail);
    }
    dict.SetString("device_id", test_data::kDeviceId);
    std::string json_string;
    base::JSONWriter::WriteWithOptions(
        dict, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json_string);
    EXPECT_CALL(config_store_, LoadSettings()).WillOnce(Return(json_string));
    ReloadDefaults(allow_endpoints_override);
  }

  void PublishCommands(const base::ListValue& commands) {
    dev_reg_->PublishCommands(commands, nullptr);
  }

  bool RefreshAccessToken(ErrorPtr* error) const {
    bool succeeded = false;
    auto callback = [](bool* succeeded, ErrorPtr* error, ErrorPtr in_error) {
      if (error) {
        *error = std::move(in_error);
        return;
      }
      *succeeded = true;
    };
    dev_reg_->RefreshAccessToken(base::Bind(
        callback, base::Unretained(&succeeded), base::Unretained(error)));
    return succeeded;
  }

  void SetAccessToken() { dev_reg_->access_token_ = test_data::kAccessToken; }

  GcdState GetGcdState() const { return dev_reg_->GetGcdState(); }

  bool HaveRegistrationCredentials() const {
    return dev_reg_->HaveRegistrationCredentials();
  }

  void RegisterDevice(const RegistrationData registration_data,
                      const RegistrationData& expected_data);

  provider::test::FakeTaskRunner task_runner_;
  provider::test::MockConfigStore config_store_;
  StrictMock<MockHttpClient> http_client_;
  base::DictionaryValue data_;
  std::unique_ptr<Config> config_;
  test::MockClock clock_;
  privet::AuthManager auth_{
      {68, 52, 36, 95, 74, 89, 25, 2,  31, 5,  65, 87, 64, 32, 17, 26,
       8,  73, 57, 16, 33, 82, 71, 10, 72, 62, 45, 1,  77, 97, 70, 24},
      {21, 6,  58, 4,  66, 13, 14, 60, 55, 22, 11, 38, 96, 40, 81, 90,
       3,  51, 50, 23, 56, 76, 47, 46, 27, 69, 20, 80, 88, 93, 15, 61},
      {},
      &clock_};
  std::unique_ptr<DeviceRegistrationInfo> dev_reg_;
  ComponentManagerImpl component_manager_{&task_runner_};
};

TEST_F(DeviceRegistrationInfoTest, GetServiceUrl) {
  EXPECT_EQ(test_data::kServiceUrl, dev_reg_->GetServiceUrl());
  std::string url = test_data::kServiceUrl;
  url += "registrationTickets";
  EXPECT_EQ(url, dev_reg_->GetServiceUrl("registrationTickets"));
  url += "?key=";
  url += test_data::kApiKey;
  EXPECT_EQ(url, dev_reg_->GetServiceUrl("registrationTickets",
                                         {{"key", test_data::kApiKey}}));
  url += "&restart=true";
  EXPECT_EQ(url, dev_reg_->GetServiceUrl(
                     "registrationTickets",
                     {
                         {"key", test_data::kApiKey}, {"restart", "true"},
                     }));
}

TEST_F(DeviceRegistrationInfoTest, GetOAuthUrl) {
  EXPECT_EQ(test_data::kOAuthUrl, dev_reg_->GetOAuthUrl());
  std::string url = test_data::kOAuthUrl;
  url += "auth?redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob&";
  url += "response_type=code&";
  url += "client_id=";
  url += test_data::kClientId;
  EXPECT_EQ(url, dev_reg_->GetOAuthUrl(
                     "auth", {{"redirect_uri", "urn:ietf:wg:oauth:2.0:oob"},
                              {"response_type", "code"},
                              {"client_id", test_data::kClientId}}));
}

TEST_F(DeviceRegistrationInfoTest, HaveRegistrationCredentials) {
  EXPECT_FALSE(HaveRegistrationCredentials());
  ReloadSettings(true, false);

  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPost, dev_reg_->GetOAuthUrl("token"),
                  HttpClient::Headers{GetFormHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(
          Invoke([](const std::string& data,
                    const HttpClient::SendRequestCallback& callback) {
            EXPECT_EQ("refresh_token", GetFormField(data, "grant_type"));
            EXPECT_EQ(test_data::kRefreshToken,
                      GetFormField(data, "refresh_token"));
            EXPECT_EQ(test_data::kClientId, GetFormField(data, "client_id"));
            EXPECT_EQ(test_data::kClientSecret,
                      GetFormField(data, "client_secret"));

            base::DictionaryValue json;
            json.SetString("access_token", test_data::kAccessToken);
            json.SetInteger("expires_in", 3600);

            callback.Run(ReplyWithJson(200, json), nullptr);
          })));

  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPost, HasSubstr("upsertLocalAuthInfo"),
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(
          Invoke([](const std::string& data,
                    const HttpClient::SendRequestCallback& callback) {
            auto dict = CreateDictionaryValue(data);
            EXPECT_TRUE(dict->Remove("localAuthInfo.clientToken", nullptr));
            EXPECT_JSON_EQ(test_data::kAuthInfo, *dict);
            base::DictionaryValue json;
            callback.Run(ReplyWithJson(200, json), nullptr);
          })));

  EXPECT_TRUE(RefreshAccessToken(nullptr));
  EXPECT_TRUE(HaveRegistrationCredentials());
}

TEST_F(DeviceRegistrationInfoTest, CheckAuthenticationFailure) {
  ReloadSettings(true, false);
  EXPECT_EQ(GcdState::kConnecting, GetGcdState());

  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPost, dev_reg_->GetOAuthUrl("token"),
                  HttpClient::Headers{GetFormHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(
          Invoke([](const std::string& data,
                    const HttpClient::SendRequestCallback& callback) {
            EXPECT_EQ("refresh_token", GetFormField(data, "grant_type"));
            EXPECT_EQ(test_data::kRefreshToken,
                      GetFormField(data, "refresh_token"));
            EXPECT_EQ(test_data::kClientId, GetFormField(data, "client_id"));
            EXPECT_EQ(test_data::kClientSecret,
                      GetFormField(data, "client_secret"));

            base::DictionaryValue json;
            json.SetString("error", "unable_to_authenticate");
            callback.Run(ReplyWithJson(400, json), nullptr);
          })));

  ErrorPtr error;
  EXPECT_FALSE(RefreshAccessToken(&error));
  EXPECT_TRUE(error->HasError("unable_to_authenticate"));
  EXPECT_EQ(GcdState::kConnecting, GetGcdState());
}

TEST_F(DeviceRegistrationInfoTest, CheckDeregistration) {
  ReloadSettings(true, false);
  EXPECT_EQ(GcdState::kConnecting, GetGcdState());

  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPost, dev_reg_->GetOAuthUrl("token"),
                  HttpClient::Headers{GetFormHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(
          Invoke([](const std::string& data,
                    const HttpClient::SendRequestCallback& callback) {
            EXPECT_EQ("refresh_token", GetFormField(data, "grant_type"));
            EXPECT_EQ(test_data::kRefreshToken,
                      GetFormField(data, "refresh_token"));
            EXPECT_EQ(test_data::kClientId, GetFormField(data, "client_id"));
            EXPECT_EQ(test_data::kClientSecret,
                      GetFormField(data, "client_secret"));

            base::DictionaryValue json;
            json.SetString("error", "invalid_grant");
            callback.Run(ReplyWithJson(400, json), nullptr);
          })));

  ErrorPtr error;
  EXPECT_FALSE(RefreshAccessToken(&error));
  EXPECT_TRUE(error->HasError("invalid_grant"));
  EXPECT_EQ(GcdState::kInvalidCredentials, GetGcdState());
  EXPECT_EQ(test_data::kCloudId, dev_reg_->GetSettings().cloud_id);
}

TEST_F(DeviceRegistrationInfoTest, GetDeviceInfo) {
  ReloadSettings(true, false);
  SetAccessToken();

  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kGet, dev_reg_->GetDeviceUrl(),
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(
          Invoke([](const std::string& data,
                    const HttpClient::SendRequestCallback& callback) {
            base::DictionaryValue json;
            json.SetString("channel.supportedType", "xmpp");
            json.SetString("deviceKind", "vendor");
            json.SetString("id", test_data::kCloudId);
            json.SetString("kind", "weave#device");
            callback.Run(ReplyWithJson(200, json), nullptr);
          })));

  bool succeeded = false;
  auto callback = [](bool* succeeded, const base::DictionaryValue& info,
                     ErrorPtr error) {
    EXPECT_FALSE(error);
    std::string id;
    EXPECT_TRUE(info.GetString("id", &id));
    EXPECT_EQ(test_data::kCloudId, id);
    *succeeded = true;
  };
  dev_reg_->GetDeviceInfo(base::Bind(callback, base::Unretained(&succeeded)));
  EXPECT_TRUE(succeeded);
}

TEST_F(DeviceRegistrationInfoTest, ReRegisterDevice) {
  ReloadSettings(true, false);

  bool done = false;
  dev_reg_->RegisterDevice(RegistrationData{test_data::kClaimTicketId},
                           base::Bind([](decltype(this) test, bool* done,
                                         ErrorPtr error) {
                             EXPECT_TRUE(error->HasError("already_registered"));
                             *done = true;
                             test->task_runner_.Break();
                             EXPECT_EQ(GcdState::kConnecting,
                                       test->GetGcdState());

                             // Validate the device info saved to storage...
                             EXPECT_EQ(test_data::kCloudId,
                                       test->dev_reg_->GetSettings().cloud_id);
                             EXPECT_EQ(
                                 test_data::kRefreshToken,
                                 test->dev_reg_->GetSettings().refresh_token);
                             EXPECT_EQ(
                                 test_data::kRobotAccountEmail,
                                 test->dev_reg_->GetSettings().robot_account);
                           }, base::Unretained(this), base::Unretained(&done)));
  task_runner_.Run();
  EXPECT_TRUE(done);
}

void DeviceRegistrationInfoTest::RegisterDevice(
    const RegistrationData registration_data,
    const RegistrationData& expected_data) {
  auto json_traits = CreateDictionaryValue(R"({
    '_foo': {
      'commands': {
        'reboot': {
          'parameters': {'delay': {'minimum': 10, 'type': 'integer'}},
          'minimalRole': 'user'
        }
      },
      'state': {
        'firmwareVersion': {'type': 'string'}
      }
    },
    '_robot': {
      'commands': {
        'jump': {
          'parameters': {'_height': {'type': 'integer'}},
          'minimalRole': 'user'
        }
      }
    }
  })");
  EXPECT_TRUE(component_manager_.LoadTraits(*json_traits, nullptr));
  EXPECT_TRUE(
      component_manager_.AddComponent("", "comp", {"_foo", "_robot"}, nullptr));
  base::StringValue ver{"1.0"};
  EXPECT_TRUE(component_manager_.SetStateProperty(
      "comp", "_foo.firmwareVersion", ver, nullptr));

  std::string ticket_url = expected_data.service_url + "registrationTickets/" +
                           expected_data.ticket_id;
  EXPECT_CALL(http_client_,
              SendRequest(HttpClient::Method::kPatch,
                          ticket_url + "?key=" + expected_data.api_key,
                          HttpClient::Headers{GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(Invoke(
          [&expected_data](const std::string& data,
                           const HttpClient::SendRequestCallback& callback) {
            auto json = test::CreateDictionaryValue(data);
            EXPECT_NE(nullptr, json.get());
            std::string value;
            EXPECT_TRUE(json->GetString("id", &value));
            EXPECT_EQ(expected_data.ticket_id, value);
            EXPECT_TRUE(
                json->GetString("deviceDraft.channel.supportedType", &value));
            EXPECT_EQ("pull", value);
            EXPECT_TRUE(json->GetString("oauthClientId", &value));
            EXPECT_EQ(expected_data.client_id, value);
            EXPECT_FALSE(json->GetString("deviceDraft.description", &value));
            EXPECT_FALSE(json->GetString("deviceDraft.location", &value));
            EXPECT_TRUE(json->GetString("deviceDraft.modelManifestId", &value));
            EXPECT_EQ("AAAAA", value);
            EXPECT_FALSE(json->GetString("deviceDraft.name", &value));
            base::DictionaryValue* dict = nullptr;
            EXPECT_FALSE(json->GetDictionary("deviceDraft.commandDefs", &dict));
            EXPECT_FALSE(json->GetDictionary("deviceDraft.state", &dict));
            EXPECT_TRUE(json->GetDictionary("deviceDraft.traits", &dict));
            auto expectedTraits = R"({
              '_foo': {
                'commands': {
                  'reboot': {
                    'parameters': {'delay': {'minimum': 10, 'type': 'integer'}},
                    'minimalRole': 'user'
                  }
                },
                'state': {
                  'firmwareVersion': {'type': 'string'}
                }
              },
              '_robot': {
                'commands': {
                  'jump': {
                    'parameters': {'_height': {'type': 'integer'}},
                    'minimalRole': 'user'
                  }
                }
              }
            })";
            EXPECT_JSON_EQ(expectedTraits, *dict);

            EXPECT_TRUE(json->GetDictionary("deviceDraft.components", &dict));
            auto expectedComponents = R"({
              'comp': {
                'traits': ['_foo', '_robot'],
                'state': {
                  '_foo': { 'firmwareVersion': '1.0' }
                }
              }
            })";
            EXPECT_JSON_EQ(expectedComponents, *dict);

            base::DictionaryValue json_resp;
            json_resp.SetString("id", test_data::kClaimTicketId);
            json_resp.SetString("kind", "weave#registrationTicket");
            json_resp.SetString("oauthClientId", test_data::kClientId);
            base::DictionaryValue* device_draft = nullptr;
            EXPECT_TRUE(json->GetDictionary("deviceDraft", &device_draft));
            device_draft = device_draft->DeepCopy();
            device_draft->SetString("id", test_data::kCloudId);
            device_draft->SetString("kind", "weave#device");
            json_resp.Set("deviceDraft", device_draft);

            callback.Run(ReplyWithJson(200, json_resp), nullptr);
          })));

  EXPECT_CALL(http_client_,
              SendRequest(HttpClient::Method::kPost,
                          ticket_url + "/finalize?key=" + expected_data.api_key,
                          HttpClient::Headers{}, _, _))
      .WillOnce(WithArgs<4>(
          Invoke([](const HttpClient::SendRequestCallback& callback) {
            base::DictionaryValue json;
            json.SetString("id", test_data::kClaimTicketId);
            json.SetString("kind", "weave#registrationTicket");
            json.SetString("oauthClientId", test_data::kClientId);
            json.SetString("userEmail", "user@email.com");
            json.SetString("deviceDraft.id", test_data::kCloudId);
            json.SetString("deviceDraft.kind", "weave#device");
            json.SetString("deviceDraft.channel.supportedType", "xmpp");
            json.SetString("robotAccountEmail", test_data::kRobotAccountEmail);
            json.SetString("robotAccountAuthorizationCode",
                           test_data::kRobotAccountAuthCode);
            callback.Run(ReplyWithJson(200, json), nullptr);
          })));

  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPost, expected_data.oauth_url + "token",
                  HttpClient::Headers{GetFormHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(Invoke([&expected_data](
          const std::string& data,
          const HttpClient::SendRequestCallback& callback) {
        EXPECT_EQ("authorization_code", GetFormField(data, "grant_type"));
        EXPECT_EQ(test_data::kRobotAccountAuthCode, GetFormField(data, "code"));
        EXPECT_EQ(expected_data.client_id, GetFormField(data, "client_id"));
        EXPECT_EQ(expected_data.client_secret,
                  GetFormField(data, "client_secret"));
        EXPECT_EQ("oob", GetFormField(data, "redirect_uri"));

        base::DictionaryValue json;
        json.SetString("access_token", test_data::kAccessToken);
        json.SetString("token_type", "Bearer");
        json.SetString("refresh_token", test_data::kRefreshToken);
        json.SetInteger("expires_in", 3600);

        callback.Run(ReplyWithJson(200, json), nullptr);
      })));

  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPost,
                  expected_data.service_url + "devices/" + test_data::kCloudId +
                      "/upsertLocalAuthInfo",
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(
          Invoke([](const std::string& data,
                    const HttpClient::SendRequestCallback& callback) {
            auto dict = CreateDictionaryValue(data);
            EXPECT_TRUE(dict->Remove("localAuthInfo.clientToken", nullptr));
            EXPECT_JSON_EQ(test_data::kAuthInfo, *dict);
            base::DictionaryValue json;
            callback.Run(ReplyWithJson(200, json), nullptr);
          })));

  bool done = false;
  dev_reg_->RegisterDevice(
      registration_data,
      base::Bind([](decltype(this) test, bool* done,
                    const RegistrationData& expected_data, ErrorPtr error) {
        *done = true;
        test->task_runner_.Break();

        EXPECT_FALSE(error);
        EXPECT_EQ(GcdState::kConnecting, test->GetGcdState());

        // Validate the device info saved to storage...
        EXPECT_EQ(test_data::kCloudId, test->dev_reg_->GetSettings().cloud_id);
        EXPECT_EQ(test_data::kRefreshToken,
                  test->dev_reg_->GetSettings().refresh_token);
        EXPECT_EQ(test_data::kRobotAccountEmail,
                  test->dev_reg_->GetSettings().robot_account);
        EXPECT_EQ(expected_data.oauth_url,
                  test->dev_reg_->GetSettings().oauth_url);
        EXPECT_EQ(expected_data.client_id,
                  test->dev_reg_->GetSettings().client_id);
        EXPECT_EQ(expected_data.client_secret,
                  test->dev_reg_->GetSettings().client_secret);
        EXPECT_EQ(expected_data.api_key, test->dev_reg_->GetSettings().api_key);
        EXPECT_EQ(expected_data.service_url,
                  test->dev_reg_->GetSettings().service_url);
        EXPECT_EQ(expected_data.xmpp_endpoint,
                  test->dev_reg_->GetSettings().xmpp_endpoint);
      }, base::Unretained(this), base::Unretained(&done), expected_data));
  task_runner_.Run();
  EXPECT_TRUE(done);
}

TEST_F(DeviceRegistrationInfoTest, RegisterDevice) {
  ReloadSettings(false, true);

  RegistrationData registration_data;
  registration_data.ticket_id = "test_ticked_id";
  registration_data.oauth_url = "https://test.oauth/";
  registration_data.client_id = "test_client_id";
  registration_data.client_secret = "test_client_secret";
  registration_data.api_key = "test_api_key";
  registration_data.service_url = "https://test.service/";
  registration_data.xmpp_endpoint = "test.xmpp:1234";

  RegisterDevice(registration_data, registration_data);
}

TEST_F(DeviceRegistrationInfoTest, RegisterDeviceWithDefaultEndpoints) {
  ReloadSettings(false, true);

  RegistrationData registration_data;
  registration_data.ticket_id = "test_ticked_id";

  RegistrationData expected_data = registration_data;
  expected_data.oauth_url = test_data::kOAuthUrl;
  expected_data.client_id = test_data::kClientId;
  expected_data.client_secret = test_data::kClientSecret;
  expected_data.api_key = test_data::kApiKey;
  expected_data.service_url = test_data::kServiceUrl;
  expected_data.xmpp_endpoint = test_data::kXmppEndpoint;

  RegisterDevice(registration_data, expected_data);
}

TEST_F(DeviceRegistrationInfoTest, RegisterDeviceEndpointsOverrideNotAllowed) {
  ReloadSettings(false, false);

  RegistrationData registration_data;
  registration_data.ticket_id = "test_ticked_id";
  registration_data.service_url = "https://test.service/";

  bool done = false;
  dev_reg_->RegisterDevice(
      registration_data,
      base::Bind(
          [](decltype(this) test, bool* done, ErrorPtr error) {
            *done = true;
            test->task_runner_.Break();
            EXPECT_TRUE(error->HasError("invalidParams"));
          },
          base::Unretained(this), base::Unretained(&done)));
  task_runner_.Run();
  EXPECT_TRUE(done);
}

TEST_F(DeviceRegistrationInfoTest, OOBRegistrationStatus) {
  // After we've been initialized, we should be either offline or
  // unregistered, depending on whether or not we've found credentials.
  EXPECT_EQ(GcdState::kUnconfigured, GetGcdState());
  // Put some credentials into our state, make sure we call that offline.
  ReloadSettings(true, false);
  EXPECT_EQ(GcdState::kConnecting, GetGcdState());
}

class DeviceRegistrationInfoUpdateCommandTest
    : public DeviceRegistrationInfoTest {
 protected:
  void SetUp() override {
    DeviceRegistrationInfoTest::SetUp();

    ReloadSettings(true, false);
    SetAccessToken();

    auto json_traits = CreateDictionaryValue(R"({
      'robot': {
        'commands': {
          '_jump': {
            'parameters': {'_height': 'integer'},
            'progress': {'progress': 'integer'},
            'results': {'status': 'string'},
            'minimalRole': 'user'
          }
        }
      }
    })");
    EXPECT_TRUE(component_manager_.LoadTraits(*json_traits, nullptr));
    EXPECT_TRUE(
        component_manager_.AddComponent("", "comp", {"robot"}, nullptr));

    command_url_ = dev_reg_->GetServiceUrl("commands/1234");

    auto commands_json = CreateValue(R"([{
      'name':'robot._jump',
      'component': 'comp',
      'id':'1234',
      'parameters': {'_height': 100},
      'minimalRole': 'user'
    }])");
    ASSERT_NE(nullptr, commands_json.get());
    const base::ListValue* command_list = nullptr;
    ASSERT_TRUE(commands_json->GetAsList(&command_list));
    PublishCommands(*command_list);
    command_ = component_manager_.FindCommand("1234");
    ASSERT_NE(nullptr, command_);
  }

  void TearDown() override {
    task_runner_.RunOnce();
    DeviceRegistrationInfoTest::TearDown();
  }

  Command* command_{nullptr};
  std::string command_url_;
};

TEST_F(DeviceRegistrationInfoUpdateCommandTest, SetProgress) {
  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPatch, command_url_,
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(Invoke([](
          const std::string& data,
          const HttpClient::SendRequestCallback& callback) {
        EXPECT_JSON_EQ((R"({"state":"inProgress","progress":{"progress":18}})"),
                       *CreateDictionaryValue(data));
        base::DictionaryValue json;
        callback.Run(ReplyWithJson(200, json), nullptr);
      })));
  EXPECT_TRUE(command_->SetProgress(*CreateDictionaryValue("{'progress':18}"),
                                    nullptr));
}

TEST_F(DeviceRegistrationInfoUpdateCommandTest, Complete) {
  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPatch, command_url_,
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(
          Invoke([](const std::string& data,
                    const HttpClient::SendRequestCallback& callback) {
            EXPECT_JSON_EQ(R"({"state":"done", "results":{"status":"Ok"}})",
                           *CreateDictionaryValue(data));
            base::DictionaryValue json;
            callback.Run(ReplyWithJson(200, json), nullptr);
          })));
  EXPECT_TRUE(
      command_->Complete(*CreateDictionaryValue("{'status': 'Ok'}"), nullptr));
}

TEST_F(DeviceRegistrationInfoUpdateCommandTest, Cancel) {
  EXPECT_CALL(
      http_client_,
      SendRequest(HttpClient::Method::kPatch, command_url_,
                  HttpClient::Headers{GetAuthHeader(), GetJsonHeader()}, _, _))
      .WillOnce(WithArgs<3, 4>(
          Invoke([](const std::string& data,
                    const HttpClient::SendRequestCallback& callback) {
            EXPECT_JSON_EQ(R"({"state":"cancelled"})",
                           *CreateDictionaryValue(data));
            base::DictionaryValue json;
            callback.Run(ReplyWithJson(200, json), nullptr);
          })));
  EXPECT_TRUE(command_->Cancel(nullptr));
}

}  // namespace weave
